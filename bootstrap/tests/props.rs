//! Properties of the compiler, checked in-process on generated programs.
//! Each takes what it expects from the generator that built its input.

use std::fmt;
use std::path::{Path, PathBuf};

use smith::diag::{self, Error, Pos};
use smith::eval::{self, Outcome};
use smith::{compile, lower};

use crate::Test;
use crate::enums;
use crate::generate::{self, Gen};
use crate::lambdas;
use crate::programs::{self, Case, PIECES, pos_after};
use crate::targets;
use crate::traits;
use crate::tuples;

pub const TESTS: &[Test] = &[
    (
        "the_shrinker_gives_the_smallest_failing_input",
        the_shrinker_gives_the_smallest_failing_input,
    ),
    (
        "the_evaluator_says_what_the_generator_built_the_program_to_print",
        the_evaluator_says_what_the_generator_built_the_program_to_print,
    ),
    (
        "the_form_every_target_receives_holds_each_function_main_reaches",
        the_form_every_target_receives_holds_each_function_main_reaches,
    ),
    (
        "the_evaluator_says_what_the_generator_built_a_program_with_traits_to_print",
        the_evaluator_says_what_the_generator_built_a_program_with_traits_to_print,
    ),
    (
        "a_program_with_type_parameters_builds_to_the_bytes_of_it_written_out",
        a_program_with_type_parameters_builds_to_the_bytes_of_it_written_out,
    ),
    (
        "a_program_with_defaults_builds_to_the_bytes_of_it_written_out",
        a_program_with_defaults_builds_to_the_bytes_of_it_written_out,
    ),
    (
        "an_implementation_with_type_parameters_applies_to_each_type_that_fits",
        an_implementation_with_type_parameters_applies_to_each_type_that_fits,
    ),
    (
        "every_placed_mistake_about_traits_is_one_error_at_its_place",
        every_placed_mistake_about_traits_is_one_error_at_its_place,
    ),
    (
        "the_evaluator_says_what_the_generator_built_a_program_with_enums_to_print",
        the_evaluator_says_what_the_generator_built_a_program_with_enums_to_print,
    ),
    (
        "a_case_written_with_a_dot_builds_to_the_bytes_of_it_written_in_full",
        a_case_written_with_a_dot_builds_to_the_bytes_of_it_written_in_full,
    ),
    (
        "every_placed_mistake_about_enums_is_one_error_at_its_place",
        every_placed_mistake_about_enums_is_one_error_at_its_place,
    ),
    (
        "the_evaluator_says_what_the_generator_built_a_program_with_tuples_to_print",
        the_evaluator_says_what_the_generator_built_a_program_with_tuples_to_print,
    ),
    (
        "every_placed_mistake_about_tuples_is_one_error_at_its_place",
        every_placed_mistake_about_tuples_is_one_error_at_its_place,
    ),
    (
        "the_evaluator_says_what_the_generator_built_a_program_with_lambdas_to_print",
        the_evaluator_says_what_the_generator_built_a_program_with_lambdas_to_print,
    ),
    (
        "the_form_every_target_receives_holds_no_function_that_takes_a_function",
        the_form_every_target_receives_holds_no_function_that_takes_a_function,
    ),
    (
        "an_expression_in_parentheses_is_the_expression",
        an_expression_in_parentheses_is_the_expression,
    ),
    (
        "every_placed_mistake_about_lambdas_is_one_error_at_its_place",
        every_placed_mistake_about_lambdas_is_one_error_at_its_place,
    ),
    (
        "a_function_main_does_not_reach_is_parsed_and_not_checked",
        a_function_main_does_not_reach_is_parsed_and_not_checked,
    ),
    (
        "every_placed_mistake_is_one_error_at_its_place",
        every_placed_mistake_is_one_error_at_its_place,
    ),
    (
        "every_input_gives_an_executable_or_an_error_with_a_place",
        every_input_gives_an_executable_or_an_error_with_a_place,
    ),
    (
        "nothing_before_the_target_seam_names_a_system_or_machine",
        nothing_before_the_target_seam_names_a_system_or_machine,
    ),
    (
        "only_smith_run_starts_another_program",
        only_smith_run_starts_another_program,
    ),
    (
        "every_error_of_the_tokens_is_reported_once_in_order",
        every_error_of_the_tokens_is_reported_once_in_order,
    ),
    (
        "every_error_of_the_syntax_is_reported_once_in_order",
        every_error_of_the_syntax_is_reported_once_in_order,
    ),
    (
        "every_error_of_the_checker_is_reported_once_in_order",
        every_error_of_the_checker_is_reported_once_in_order,
    ),
    (
        "an_error_shows_its_line_with_a_mark_under_its_column",
        an_error_shows_its_line_with_a_mark_under_its_column,
    ),
    (
        "a_mistake_in_the_text_hides_every_later_stage",
        a_mistake_in_the_text_hides_every_later_stage,
    ),
];

fn the_shrinker_gives_the_smallest_failing_input() {
    // Known to fail: some program prints an `é`.
    let prints_no_e = |case: &Case| {
        assert!(!String::from_utf8_lossy(&case.stdout).contains('é'));
    };
    let failure = generate::find_failure(1, 1000, &programs::program, &prints_no_e)
        .expect("some program prints `é`");
    // The smallest such program prints `é` and nothing else, and no choice
    // can be made smaller.
    assert_eq!(
        failure.input.stdout,
        "é\n".as_bytes(),
        "{:?}",
        failure.input
    );
    let again = generate::shrink(failure.choices.clone(), &programs::program, &prints_no_e);
    assert_eq!(again.choices, failure.choices, "{:?}", again.input);
    let simplest = programs::program(&mut Gen::replay(&[]));
    let mut longer = failure.input.source.clone();
    longer.retain(|b| !"é".as_bytes().contains(b));
    assert_eq!(
        longer.len(),
        simplest.source.len(),
        "only the literal grew: {:?}",
        failure.input
    );
}

fn the_evaluator_says_what_the_generator_built_the_program_to_print() {
    generate::check(2, 500, programs::program, |case| {
        let program = lower(&case.source).unwrap_or_else(|e| panic!("{e:?}"));
        let expected = Outcome {
            stdout: case.stdout.clone(),
            status: 0,
        };
        assert_eq!(eval::run(&program), expected);
    });
}

fn the_form_every_target_receives_holds_each_function_main_reaches() {
    generate::check(11, 300, programs::program, |case| {
        let program = lower(&case.source).unwrap_or_else(|e| panic!("{e:?}"));
        assert_eq!(program.functions.len(), case.reached);
    });
}

/// A generated program, and a function that `main` does not reach, which
/// holds a mistake for the checker.
fn program_and_unreached(g: &mut Gen) -> (Case, &'static str) {
    (programs::program(g), *g.pick(programs::UNREACHED))
}

fn a_function_main_does_not_reach_is_parsed_and_not_checked() {
    generate::check(12, 200, program_and_unreached, |(case, unreached)| {
        let program = lower(&case.source).unwrap_or_else(|e| panic!("{e:?}"));
        let mut text = String::from_utf8(case.source.clone()).unwrap();
        text.push_str("\n\n");
        // The program may hold a definition named `unused` already.
        let renamed = unreached
            .replace("unused", "spare")
            .replace("Unused", "Spare");
        let with_unreached = format!("{text}{renamed}\n");
        assert_eq!(lower(with_unreached.as_bytes()), Ok(program));
        // A mistake in its syntax is still found, as it is parsed.
        let broken = format!("{text}fn broken() {{ x = ");
        let at = pos_after(&broken);
        let found = errors(&format!("{broken}1 }}\n"));
        assert_eq!(found.iter().map(|e| e.pos).collect::<Vec<_>>(), [at]);
    });
}

fn the_evaluator_says_what_the_generator_built_a_program_with_traits_to_print() {
    generate::check(14, 200, traits::program, |case| {
        let program = lower(case.source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
        let expected = Outcome {
            stdout: case.stdout.clone(),
            status: 0,
        };
        assert_eq!(eval::run(&program), expected);
        assert_eq!(program.functions.len(), case.copies, "one for each copy");
    });
}

/// A generated program with traits, and which of `targets` targets to
/// build it for.
fn program_and_target(g: &mut Gen, targets: usize) -> (traits::TraitCase, usize) {
    (traits::program(g), g.below(targets))
}

fn a_program_with_type_parameters_builds_to_the_bytes_of_it_written_out() {
    let targets = targets::buildable();
    let choose = |g: &mut Gen| program_and_target(g, targets.len());
    generate::check(15, 60, choose, |(case, target)| {
        // Every target receives the same form of the program, so the two
        // build to the same bytes for every target when that form is the
        // same.
        let built = lower(case.source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
        let written_out = lower(case.written_out.as_bytes())
            .unwrap_or_else(|e| panic!("{e:?} in {}", case.written_out));
        assert!(built == written_out, "differs from\n{}", case.written_out);
        let target = targets[*target];
        let built = compile(case.source.as_bytes(), target).unwrap();
        let written_out = compile(case.written_out.as_bytes(), target).unwrap();
        assert!(built == written_out, "{} differs", target.name());
    });
}

/// A generated program, which may leave arguments and fields to their
/// defaults, and which of `targets` targets to build it for.
fn program_with_defaults_and_target(g: &mut Gen, targets: usize) -> (Case, usize) {
    (programs::program(g), g.below(targets))
}

fn a_program_with_defaults_builds_to_the_bytes_of_it_written_out() {
    let targets = targets::buildable();
    let choose = |g: &mut Gen| program_with_defaults_and_target(g, targets.len());
    generate::check(20, 60, choose, |(case, target)| {
        let written_out = String::from_utf8_lossy(&case.written_out);
        let built = lower(&case.source).unwrap_or_else(|e| panic!("{e:?}"));
        let expanded =
            lower(&case.written_out).unwrap_or_else(|e| panic!("{e:?} in\n{written_out}"));
        assert!(built == expanded, "differs from\n{written_out}");
        let target = targets[*target];
        let built = compile(&case.source, target).unwrap();
        let expanded = compile(&case.written_out, target).unwrap();
        assert!(built == expanded, "{} differs", target.name());
    });
}

fn the_evaluator_says_what_the_generator_built_a_program_with_enums_to_print() {
    generate::check(21, 300, enums::program, |case| {
        let program = lower(case.source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
        let expected = Outcome {
            stdout: case.stdout.clone(),
            status: 0,
        };
        assert_eq!(eval::run(&program), expected);
    });
}

/// A generated program with enums, and which of `targets` targets to
/// build it for.
fn program_with_enums_and_target(g: &mut Gen, targets: usize) -> (enums::EnumCase, usize) {
    (enums::program(g), g.below(targets))
}

fn a_case_written_with_a_dot_builds_to_the_bytes_of_it_written_in_full() {
    let targets = targets::buildable();
    let choose = |g: &mut Gen| program_with_enums_and_target(g, targets.len());
    generate::check(22, 60, choose, |(case, target)| {
        let built = lower(case.source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
        let in_full =
            lower(case.in_full.as_bytes()).unwrap_or_else(|e| panic!("{e:?} in\n{}", case.in_full));
        assert!(built == in_full, "differs from\n{}", case.in_full);
        let target = targets[*target];
        let built = compile(case.source.as_bytes(), target).unwrap();
        let in_full = compile(case.in_full.as_bytes(), target).unwrap();
        assert!(built == in_full, "{} differs", target.name());
    });
}

fn every_placed_mistake_about_enums_is_one_error_at_its_place() {
    generate::check(23, 500, enums::program_with_mistake, |case| {
        let expected = case.mistake.as_ref().expect("a placed mistake");
        assert_mistake(case.source.as_bytes(), expected);
    });
}

fn the_evaluator_says_what_the_generator_built_a_program_with_tuples_to_print() {
    generate::check(26, 300, tuples::program, |case| {
        let program = lower(case.source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
        let expected = Outcome {
            stdout: case.stdout.clone(),
            status: 0,
        };
        assert_eq!(eval::run(&program), expected);
    });
}

fn every_placed_mistake_about_tuples_is_one_error_at_its_place() {
    generate::check(27, 500, tuples::program_with_mistake, |case| {
        let expected = case.mistake.as_ref().expect("a placed mistake");
        assert_mistake(case.source.as_bytes(), expected);
    });
}

fn the_evaluator_says_what_the_generator_built_a_program_with_lambdas_to_print() {
    generate::check(29, 300, lambdas::program, |case| {
        let program = lower(case.source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
        let expected = Outcome {
            stdout: case.stdout.clone(),
            status: 0,
        };
        assert_eq!(eval::run(&program), expected);
    });
}

/// The form every target receives holds a function for each function with
/// a name that runs, and none for a function that takes a function, which
/// is lowered in place with the lambdas it is given. Such a function would
/// need a value of a function type, as a function with type parameters
/// would need a type parameter.
fn the_form_every_target_receives_holds_no_function_that_takes_a_function() {
    generate::check(30, 300, lambdas::program, |case| {
        let program = lower(case.source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
        assert_eq!(program.functions.len(), case.copies);
    });
}

fn an_expression_in_parentheses_is_the_expression() {
    generate::check(33, 100, lambdas::program, |case| {
        let program = lower(case.source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
        let wrapped = lower(case.parenthesized.as_bytes())
            .unwrap_or_else(|e| panic!("{e:?} in {}", case.parenthesized));
        assert!(program == wrapped, "differs from\n{}", case.parenthesized);
    });
}

fn every_placed_mistake_about_lambdas_is_one_error_at_its_place() {
    generate::check(31, 500, lambdas::program_with_mistake, |case| {
        let expected = case.mistake.as_ref().expect("a placed mistake");
        assert_mistake(case.source.as_bytes(), expected);
    });
}

fn an_implementation_with_type_parameters_applies_to_each_type_that_fits() {
    generate::check(16, 200, traits::boxes, |(source, stdout)| {
        let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
        assert_eq!(eval::run(&program).stdout, *stdout);
    });
}

/// Checks that a file with one placed mistake gives one error, at its
/// place, that mentions what it must, with the fix it must have.
fn assert_mistake(source: &[u8], expected: &programs::Expected) {
    let found = lower(source).expect_err("an error");
    assert_well_formed(source, &found);
    assert_eq!(found.len(), 1, "{found:?}");
    let e = &found[0];
    assert_eq!(e.pos, expected.at, "{e}");
    assert!(
        e.message.contains(&expected.mentions),
        "{e} does not mention {}",
        expected.mentions
    );
    match (&expected.fix, &e.fix) {
        (None, None) => {}
        (Some(wanted), Some(fix)) => {
            assert!(fix.contains(wanted.as_str()), "{fix} lacks {wanted}")
        }
        (wanted, fix) => panic!("the fix is {fix:?}, not {wanted:?}"),
    }
    let printed = diag::render("f.smith", source, &found);
    let first = format!("f.smith:{}:{}: {}\n", e.pos.line, e.pos.col, e.message);
    assert!(printed.starts_with(&first), "{printed}");
}

fn every_placed_mistake_is_one_error_at_its_place() {
    generate::check(4, 1000, programs::program_with_mistake, |case| {
        assert_mistake(
            &case.source,
            case.mistake.as_ref().expect("a placed mistake"),
        );
    });
}

fn every_placed_mistake_about_traits_is_one_error_at_its_place() {
    generate::check(17, 300, traits::program_with_mistake, |case| {
        let expected = case.mistake.as_ref().expect("a placed mistake");
        assert_mistake(case.source.as_bytes(), expected);
    });
}

/// Bytes, shown as text.
pub struct Bytes(pub Vec<u8>);

impl fmt::Debug for Bytes {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:?}", String::from_utf8_lossy(&self.0))
    }
}

/// A generated program, with or without a mistake, damaged in a few places,
/// or now and then bytes at random, and which of `targets` targets to
/// build it for.
fn damaged(g: &mut Gen, targets: usize) -> (Bytes, usize) {
    let target = g.below(targets);
    (bytes(g), target)
}

pub fn bytes(g: &mut Gen) -> Bytes {
    const NOISE: &[&[u8]] = &[
        b"\"",
        b"{",
        b"}",
        b"(",
        b")",
        b"\\",
        b"\n",
        b" ",
        b"//",
        b",",
        b"!",
        b"fn",
        b"main",
        b"print",
        b"console",
        b":",
        b"=",
        b"0",
        b"\xff",
        b"\xc3",
        b"\xe6\x97\xa5",
        b"\t",
        b"\r",
    ];
    if g.one_in(10) {
        return Bytes((0..g.below(64)).map(|_| g.below(256) as u8).collect());
    }
    let mut source = match g.below(8) {
        0 => programs::program_with_mistake(g).source,
        1 => programs::program(g).source,
        2 => traits::program_with_mistake(g).source.into_bytes(),
        3 => traits::program(g).source.into_bytes(),
        4 => enums::program_with_mistake(g).source.into_bytes(),
        5 => enums::program(g).source.into_bytes(),
        6 => tuples::program_with_mistake(g).source.into_bytes(),
        _ => tuples::program(g).source.into_bytes(),
    };
    for _ in 0..1 + g.below(4) {
        let at = g.below(source.len() + 1);
        match g.below(3) {
            0 => {
                let piece = g.pick(NOISE);
                source.splice(at..at, piece.iter().copied());
            }
            1 if at < source.len() => {
                source.remove(at);
            }
            _ => source.truncate(at),
        }
    }
    Bytes(source)
}

fn every_input_gives_an_executable_or_an_error_with_a_place() {
    let targets = targets::buildable();
    let choose = |g: &mut Gen| damaged(g, targets.len());
    generate::check(5, 800, choose, |(Bytes(source), target)| {
        if let Err(errors) = compile(source, targets[*target]) {
            assert_well_formed(source, &errors);
        }
    });
}

/// The files that come before the target seam.
const BEFORE_THE_SEAM: &[(&str, &str)] = &[
    ("diag.rs", include_str!("../src/diag.rs")),
    ("lexer.rs", include_str!("../src/lexer.rs")),
    ("parser.rs", include_str!("../src/parser.rs")),
    ("check.rs", include_str!("../src/check.rs")),
    ("ir.rs", include_str!("../src/ir.rs")),
    ("eval.rs", include_str!("../src/eval.rs")),
];

fn nothing_before_the_target_seam_names_a_system_or_machine() {
    for (file, text) in BEFORE_THE_SEAM {
        let text = text.to_lowercase();
        for word in ["linux", "x86", "x64", "syscall", "system call"] {
            assert!(!text.contains(word), "{file} mentions {word}");
        }
    }
}

fn only_smith_run_starts_another_program() {
    for (file, text) in sources(&Path::new(env!("CARGO_MANIFEST_DIR")).join("src")) {
        if !file.ends_with("src/main.rs") {
            assert!(
                !text.contains("Command"),
                "{} starts a program",
                file.display()
            );
        }
    }
    let main = include_str!("../src/main.rs");
    let starts: Vec<_> = main.match_indices("Command::").collect();
    assert_eq!(starts.len(), 1, "main.rs starts programs in one place");
    let run = main.find("\nfn run(").expect("main.rs has fn run");
    let next = main[run + 1..]
        .find("\nfn ")
        .map_or(main.len(), |i| run + 1 + i);
    assert!(
        (run..next).contains(&starts[0].0),
        "only run starts a program"
    );
}

/// Every Rust file in the folder and the folders inside it.
fn sources(dir: &Path) -> Vec<(PathBuf, String)> {
    let mut found = Vec::new();
    for entry in std::fs::read_dir(dir).unwrap() {
        let path = entry.unwrap().path();
        if path.is_dir() {
            found.extend(sources(&path));
        } else if path.extension().is_some_and(|e| e == "rs") {
            let text = std::fs::read_to_string(&path).unwrap();
            found.push((path, text));
        }
    }
    assert!(!found.is_empty());
    found
}

/// Checks what must hold of the errors for any input: there is at least
/// one, each is at a place inside the file, they are in the order of their
/// places with none twice, advice is never in a message, and the printed
/// form holds no control characters other than line breaks.
fn assert_well_formed(source: &[u8], errors: &[Error]) {
    let shown = String::from_utf8_lossy(source);
    let text = diag::text(source);
    let lines: Vec<&str> = text.split('\n').collect();
    assert!(!errors.is_empty(), "no error for {shown:?}");
    for e in errors {
        let Pos { line, col } = e.pos;
        assert!(line >= 1 && line <= lines.len(), "{e} for {shown:?}");
        let length = lines[line - 1].chars().count();
        assert!(col >= 1 && col <= length + 1, "{e} for {shown:?}");
        assert!(!e.message.contains("write `"), "{e} holds its fix");
    }
    for pair in errors.windows(2) {
        assert!(pair[0].pos <= pair[1].pos, "out of order for {shown:?}");
        assert_ne!(pair[0], pair[1], "reported twice for {shown:?}");
    }
    let printed = diag::render("f.smith", source, errors);
    assert!(
        printed.chars().all(|c| c == '\n' || !c.is_control()),
        "{printed:?}"
    );
    let fixes = printed.lines().filter(|l| l.starts_with("  fix: ")).count();
    assert_eq!(fixes, errors.iter().filter(|e| e.fix.is_some()).count());
}

fn errors(source: &str) -> Vec<Error> {
    match lower(source.as_bytes()) {
        Ok(_) => panic!("expected an error for {source:?}"),
        Err(errors) => errors,
    }
}

/// A piece of a string literal that holds a mistake, and what its error
/// must mention.
const BAD_PIECES: &[(&str, &str)] = &[
    ("\\q", "unknown escape"),
    ("\\0", "unknown escape"),
    ("\\ ", "unknown escape"),
    ("\\é", "unknown escape"),
    ("}", "closes no expression"),
    ("{x}", "not supported yet"),
];

/// A file whose only mistakes are in the tokens, each at a known place,
/// along with what each error must mention. It also holds a mistake for the
/// checker, if `main` lacks `console`, which must not be reported.
fn file_with_token_errors(g: &mut Gen) -> (String, Vec<(Pos, &'static str)>) {
    let mut source = String::new();
    let mut expected = Vec::new();
    source.push_str(g.pick(&["fn main() ! console {", "fn main() {"]));
    source.push_str(g.pick(&[" ", "\n  ", "\n\t"]));
    if g.one_in(3) {
        expected.push((pos_after(&source), "unexpected character"));
        source.push_str(g.pick(&["#", "$", "?"]));
    }
    source.push_str("print(\"");
    for _ in 0..1 + g.below(8) {
        if g.one_in(2) {
            let (written, what) = g.pick(BAD_PIECES);
            expected.push((pos_after(&source), *what));
            source.push_str(written);
        } else {
            source.push_str(g.pick(PIECES).0);
        }
    }
    if expected
        .iter()
        .all(|(_, what)| *what == "unexpected character")
    {
        expected.push((pos_after(&source), "closes no expression"));
        source.push('}');
    }
    source.push_str("\")");
    source.push_str(" }");
    if g.one_in(3) {
        source.push(' ');
        expected.push((pos_after(&source), "own line"));
        source.push_str("// note");
    }
    source.push('\n');
    (source, expected)
}

/// A file whose mistakes are all in its syntax, one in each statement or
/// definition that holds one, at known places. Its `main` lacks `console`,
/// which must not be reported.
fn file_with_syntax_errors(g: &mut Gen) -> (String, Vec<(Pos, &'static str)>) {
    // Each is written as the text before the mistake, and the rest. A
    // statement or a definition with no mistake has no error to mention.
    type Line = (&'static str, &'static str, Option<&'static str>);
    const STATEMENTS: &[Line] = &[
        ("", "print(\"a\")", None),
        ("", "x = \"a\"", None),
        ("x = ", "1", Some("not supported yet")),
        ("var x = ", "1", Some("not supported yet")),
        ("print(\"a\" ", "\"b\")", Some("`,` or `)`")),
        ("print(\"a\")", "[0]", Some("not supported yet")),
        ("x.y ", "+= \"a\"", Some("not supported yet")),
        ("", ")", Some("statement")),
    ];
    const ITEMS: &[Line] = &[
        ("struct A { x: str = ", "1 }", Some("not supported yet")),
        ("", "struct C { x: str = \"a\" }", None),
        ("fn d(a: str = ", ") {}", Some("an expression")),
        ("", "struct B {\n  x: f32\n}", None),
        ("", "use std.console (input)", Some("not supported yet")),
        ("", ")", Some("definition")),
        ("", "fn f() ! console { print(\"a\") }", None),
        ("fn g()\n{\n  x = ", "1\n}", Some("not supported yet")),
        ("fn h(a: str ", "b: str) {}", Some("`,` or `)`")),
    ];
    {
        let mut source = String::new();
        let mut expected = Vec::new();
        let items = g.below(3);
        let main_at = g.below(items + 1);
        for i in 0..=items {
            let (lines, indent): (Vec<&Line>, &str) = if i == main_at {
                source.push_str("fn main() {\n");
                (
                    (0..1 + g.below(4)).map(|_| g.pick(STATEMENTS)).collect(),
                    "  ",
                )
            } else {
                (vec![g.pick(ITEMS)], "")
            };
            for (before, rest, bad) in lines {
                source.push_str(indent);
                source.push_str(before);
                if let Some(what) = bad {
                    expected.push((pos_after(&source), *what));
                }
                source.push_str(rest);
                source.push('\n');
            }
            if i == main_at {
                source.push_str("}\n");
            }
        }
        if expected.is_empty() {
            // Every statement and definition came out right, so one more
            // definition holds the mistake.
            expected.push((pos_after(&source), "definition"));
            source.push_str(")\n");
        }
        (source, expected)
    }
}

/// A file whose mistakes are all for the checker, with the errors it must
/// give.
fn file_with_check_errors(g: &mut Gen) -> (String, Vec<(Pos, String)>) {
    loop {
        let mut source = String::from("fn main()");
        let mut expected = Vec::new();
        let mut listed: Vec<&str> = Vec::new();
        let mut unknown = false;
        for i in 0..g.below(4) {
            source.push_str(if i == 0 { " ! " } else { ", " });
            let effect = *g.pick(&["console", "fs", "alloc", "consol", "net"]);
            let at = pos_after(&source);
            source.push_str(effect);
            if listed.contains(&effect) {
                expected.push((at, format!("`{effect}` effect is already listed")));
                continue;
            }
            listed.push(effect);
            match effect {
                "console" => {}
                "consol" => {
                    unknown = true;
                    expected.push((at, "no effect named `consol`".to_string()));
                }
                _ => expected.push((at, format!("the `{effect}` effect is not supported yet"))),
            }
        }
        source.push_str(" {\n  ");
        let callee = *g.pick(&["print", "print", "prin", "main"]);
        let at = pos_after(&source);
        source.push_str(callee);
        source.push('(');
        let args = g.below(3);
        for i in 0..args {
            if i > 0 {
                source.push_str(", ");
            }
            let arg = pos_after(&source);
            match (callee, i) {
                ("print", 1) => expected.push((arg, "`end` and `to`".to_string())),
                ("main", 0) => expected.push((arg, "takes 0 arguments".to_string())),
                _ => {}
            }
            source.push_str("\"a\"");
        }
        match callee {
            "print" => {
                if args == 0 {
                    expected.push((pos_after(&source), "needs the text".to_string()));
                }
                if !listed.contains(&"console") && !unknown {
                    expected.push((at, "`console` effect".to_string()));
                }
            }
            "main" => expected.push((at, "not supported yet".to_string())),
            _ => expected.push((at, "no function named `prin`".to_string())),
        }
        source.push_str(")\n}\n");
        if !expected.is_empty() {
            expected.sort_by_key(|(pos, _)| *pos);
            return (source, expected);
        }
    }
}

fn assert_errors(source: &str, expected: &[(Pos, &str)]) {
    let found = errors(source);
    assert_well_formed(source.as_bytes(), &found);
    let places: Vec<Pos> = found.iter().map(|e| e.pos).collect();
    let wanted: Vec<Pos> = expected.iter().map(|(pos, _)| *pos).collect();
    assert_eq!(places, wanted, "{found:?}");
    for (e, (_, what)) in found.iter().zip(expected) {
        assert!(e.message.contains(what), "{e} lacks {what}");
    }
}

fn every_error_of_the_tokens_is_reported_once_in_order() {
    generate::check(7, 500, file_with_token_errors, |(source, expected)| {
        assert_errors(source, expected);
    });
}

fn every_error_of_the_syntax_is_reported_once_in_order() {
    generate::check(8, 500, file_with_syntax_errors, |(source, expected)| {
        assert_errors(source, expected);
    });
}

fn every_error_of_the_checker_is_reported_once_in_order() {
    generate::check(9, 500, file_with_check_errors, |(source, expected)| {
        let expected: Vec<(Pos, &str)> = expected.iter().map(|(p, w)| (*p, w.as_str())).collect();
        assert_errors(source, &expected);
    });
}

/// A file with one unknown escape, after pieces that take a known number of
/// places on a terminal, and how many places come before the escape.
fn file_with_a_mark(g: &mut Gen) -> (String, usize) {
    let blank = *g.pick(&["", "\n", "\n\n\n\n\n\n\n\n\n\n"]);
    let indent = *g.pick(&["", "  ", "\t", " \t "]);
    let mut source = format!("fn main() ! console {{{blank}\n{indent}print(\"");
    let mut places = indent.len() + "print(\"".len();
    for _ in 0..g.below(8) {
        let (written, _, width) = g.pick(PIECES);
        source.push_str(written);
        places += width;
    }
    source.push_str("\\q\") }\n");
    (source, places)
}

fn an_error_shows_its_line_with_a_mark_under_its_column() {
    generate::check(10, 500, file_with_a_mark, |(source, places)| {
        let found = errors(source);
        let e = &found[0];
        let line = source.lines().nth(e.pos.line - 1).unwrap();
        let printed = diag::render("f.smith", source.as_bytes(), &found);
        let number = e.pos.line.to_string();
        let gutter = " ".repeat(number.len());
        let expected = format!(
            "f.smith:{number}:{}: {}\n  {number} | {}\n  {gutter} | {}^\n",
            e.pos.col,
            e.message,
            line.replace('\t', " "),
            " ".repeat(*places),
        );
        assert_eq!(printed, expected);
    });
}

fn a_mistake_in_the_text_hides_every_later_stage() {
    let parts: &[&[u8]] = &[
        b"fn main() {",
        b"\r",
        b"\n  print(\"\\q",
        b"\xff",
        b"\")",
        b"\r",
        b"\n}",
        b"\r",
        b"\n",
    ];
    let mut source = Vec::new();
    let mut expected = Vec::new();
    for part in parts {
        if matches!(part, [b'\r'] | [0xff]) {
            expected.push(pos_after(&diag::text(&source)));
        }
        source.extend_from_slice(part);
    }
    let found = lower(&source).unwrap_err();
    assert_well_formed(&source, &found);
    let places: Vec<_> = found.iter().map(|e| e.pos).collect();
    assert_eq!(places, expected);
    source.retain(|b| *b != b'\r' && *b != 0xff);
    let found = lower(&source).unwrap_err();
    assert_eq!(found.len(), 1);
    assert!(found[0].message.contains("unknown escape"));
}
