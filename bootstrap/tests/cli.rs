//! The `smith build` and `smith run` commands, and the cases in `cases/`.
//!
//! A case is a `.smith` file with either a `.out` file beside it, holding
//! what the program prints, or a `.err` file, holding the error that
//! building it reports.

use std::path::{Path, PathBuf};
use std::process::{Command, Output};
use std::sync::atomic::{AtomicUsize, Ordering};

use smith::target::Target;

use crate::Test;
use crate::targets::{self, any, case_files, updating};

pub const ONCE: &[Test] = &[
    (
        "the_cases_build_or_report_their_error",
        the_cases_build_or_report_their_error,
    ),
    (
        "build_writes_the_executable_to_the_folder_it_ran_in",
        build_writes_the_executable_to_the_folder_it_ran_in,
    ),
    (
        "smith_build_twice_writes_identical_bytes",
        smith_build_twice_writes_identical_bytes,
    ),
    (
        "main_smith_is_the_entry_and_takes_the_folder_name",
        main_smith_is_the_entry_and_takes_the_folder_name,
    ),
    (
        "an_unknown_target_is_an_error_that_names_the_targets",
        an_unknown_target_is_an_error_that_names_the_targets,
    ),
    (
        "run_rejects_a_target_other_than_this_machine",
        run_rejects_a_target_other_than_this_machine,
    ),
    ("a_file_must_be_named_smith", a_file_must_be_named_smith),
    (
        "timings_list_every_stage_in_order_and_change_nothing_else",
        timings_list_every_stage_in_order_and_change_nothing_else,
    ),
    (
        "a_failed_build_lists_the_stages_that_ran_after_its_errors",
        a_failed_build_lists_the_stages_that_ran_after_its_errors,
    ),
];

/// The tests whose result depends on the kind of machine they run on.
pub const MACHINE: &[Test] = &[
    (
        "the_cases_run_as_their_out_files_say",
        the_cases_run_as_their_out_files_say,
    ),
    (
        "without_a_flag_the_target_is_this_machine",
        without_a_flag_the_target_is_this_machine,
    ),
    (
        "run_prints_the_timings_before_the_program_runs",
        run_prints_the_timings_before_the_program_runs,
    ),
];

const HELLO: &str = "fn main() ! console { print(\"hello world\") }\n";

/// A new, empty folder for one test.
fn folder(name: &str) -> PathBuf {
    static COUNT: AtomicUsize = AtomicUsize::new(0);
    let dir = std::env::temp_dir()
        .join(format!(
            "smith-cli-{}-{}",
            std::process::id(),
            COUNT.fetch_add(1, Ordering::Relaxed)
        ))
        .join(name);
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

fn smith(dir: &Path, args: &[&str]) -> Output {
    Command::new(env!("CARGO_BIN_EXE_smith"))
        .args(args)
        .current_dir(dir)
        .output()
        .unwrap()
}

fn stderr(output: &Output) -> String {
    String::from_utf8_lossy(&output.stderr).into_owned()
}

/// `smith build` for a target, of a file in the folder.
fn build(dir: &Path, target: Target, file: &str) -> Output {
    smith(dir, &["build", "--target", target.name(), file])
}

fn the_cases_build_or_report_their_error() {
    for source in case_files() {
        let name = source.file_stem().unwrap().to_str().unwrap();
        let file = format!("{name}.smith");
        let out = source.with_extension("out");
        let err = source.with_extension("err");
        for target in targets::buildable() {
            let dir = folder(name);
            std::fs::copy(&source, dir.join(&file)).unwrap();
            let built = build(&dir, target, &file);
            let shown = format!("{file} for {}", target.name());
            let executable = target.executable_name(name);
            if updating() {
                update(&source, &built);
                break;
            }
            if out.exists() {
                assert!(built.status.success(), "{shown}: {}", stderr(&built));
                assert!(
                    dir.join(&executable).is_file(),
                    "{shown} writes {executable}"
                );
            } else {
                assert!(!built.status.success(), "{shown} is rejected");
                let expected = std::fs::read_to_string(&err)
                    .unwrap_or_else(|_| panic!("{file} has a .out or .err file beside it"));
                assert_eq!(stderr(&built), expected, "{shown}");
                assert!(
                    !dir.join(&executable).exists(),
                    "{shown} writes no executable"
                );
            }
        }
    }
}

fn the_cases_run_as_their_out_files_say() {
    if updating() {
        return;
    }
    for target in targets::runnable() {
        for source in cases("out") {
            let name = source.file_stem().unwrap().to_str().unwrap();
            let file = format!("{name}.smith");
            let dir = folder(name);
            std::fs::copy(&source, dir.join(&file)).unwrap();
            let ran = smith(&dir, &["run", "--target", target.name(), &file]);
            assert_eq!(ran.status.code(), Some(0), "{file}: {}", stderr(&ran));
            let expected = std::fs::read(source.with_extension("out")).unwrap();
            assert_eq!(ran.stdout, expected, "{file} for {}", target.name());
        }
    }
}

/// Rewrites a case's expected output from what building it gave: the
/// errors in its `.err` file, or what the evaluator says the program prints
/// in its `.out` file.
fn update(source: &Path, built: &Output) {
    let out = source.with_extension("out");
    let err = source.with_extension("err");
    let (write, remove, bytes) = if built.status.success() {
        let program = smith::lower(&std::fs::read(source).unwrap()).unwrap();
        (out, err, smith::eval::run(&program).stdout)
    } else {
        (err, out, built.stderr.clone())
    };
    std::fs::write(write, bytes).unwrap();
    if remove.exists() {
        std::fs::remove_file(remove).unwrap();
    }
}

fn build_writes_the_executable_to_the_folder_it_ran_in() {
    let dir = folder("work");
    std::fs::create_dir_all(dir.join("src")).unwrap();
    std::fs::write(dir.join("src/hello.smith"), HELLO).unwrap();
    std::fs::write(dir.join("hello"), "an old file").unwrap();
    let built = build(&dir, any(), "src/hello.smith");
    assert!(built.status.success(), "{}", stderr(&built));
    assert!(!dir.join("src/hello").exists());
    assert_ne!(std::fs::read(dir.join("hello")).unwrap(), b"an old file");
}

fn smith_build_twice_writes_identical_bytes() {
    let first = folder("first");
    let second = folder("second");
    for dir in [&first, &second] {
        std::fs::write(dir.join("hello.smith"), HELLO).unwrap();
        let built = build(dir, any(), "hello.smith");
        assert!(built.status.success(), "{}", stderr(&built));
    }
    let bytes = std::fs::read(first.join("hello")).unwrap();
    assert_eq!(bytes, std::fs::read(second.join("hello")).unwrap());
    let again = build(&first, any(), "hello.smith");
    assert!(again.status.success());
    assert_eq!(bytes, std::fs::read(first.join("hello")).unwrap());
}

fn main_smith_is_the_entry_and_takes_the_folder_name() {
    let dir = folder("greeter");
    std::fs::write(dir.join("main.smith"), HELLO).unwrap();
    let built = smith(&dir, &["build", "--target", any().name()]);
    assert!(built.status.success(), "{}", stderr(&built));
    assert!(dir.join("greeter").is_file());
}

fn without_a_flag_the_target_is_this_machine() {
    let dir = folder("host");
    std::fs::write(dir.join("hello.smith"), HELLO).unwrap();
    let built = smith(&dir, &["build", "hello.smith"]);
    match Target::host() {
        Some(host) if targets::runnable().contains(&host) => {
            assert!(built.status.success(), "{}", stderr(&built));
            let ran = smith(&dir, &["run", "hello.smith"]);
            assert_eq!(ran.status.code(), Some(0));
            assert_eq!(ran.stdout, b"hello world\n");
        }
        Some(host) => {
            assert!(!built.status.success());
            assert!(stderr(&built).contains(host.name()), "{}", stderr(&built));
        }
        None => assert!(stderr(&built).contains("is not a target")),
    }
}

fn an_unknown_target_is_an_error_that_names_the_targets() {
    let dir = folder("unknown");
    std::fs::write(dir.join("hello.smith"), HELLO).unwrap();
    let built = smith(&dir, &["build", "--target", "vax", "hello.smith"]);
    assert!(!built.status.success());
    for target in Target::ALL {
        assert!(stderr(&built).contains(target.name()), "{}", stderr(&built));
    }
}

fn run_rejects_a_target_other_than_this_machine() {
    let dir = folder("other");
    std::fs::write(dir.join("hello.smith"), HELLO).unwrap();
    let other = Target::ALL
        .iter()
        .find(|t| Target::host() != Some(**t))
        .unwrap()
        .name();
    let ran = smith(&dir, &["run", "--target", other, "hello.smith"]);
    assert!(!ran.status.success());
    assert!(stderr(&ran).contains(other), "{}", stderr(&ran));
    assert!(!dir.join("hello").exists());
}

fn a_file_must_be_named_smith() {
    let dir = folder("named");
    std::fs::write(dir.join("hello.txt"), HELLO).unwrap();
    let built = build(&dir, any(), "hello.txt");
    assert!(!built.status.success());
    assert_eq!(
        std::fs::read_to_string(dir.join("hello.txt")).unwrap(),
        HELLO
    );
}

/// The stage lines of a `--timings` report: each stage's name and its time
/// in milliseconds, with `total` last.
fn timing_lines(text: &str) -> Vec<(String, f64)> {
    let lines: Vec<&str> = text.lines().collect();
    let first = lines
        .iter()
        .position(|l| l.starts_with("lex "))
        .unwrap_or_else(|| panic!("no timings in {text:?}"));
    lines[first..]
        .iter()
        .map(|l| {
            let mut words = l.split_whitespace();
            let name = words.next().unwrap().to_string();
            let time = words.next().unwrap().parse().unwrap();
            assert_eq!(words.next(), Some("ms"), "{l}");
            assert_eq!(words.next(), None, "{l}");
            (name, time)
        })
        .collect()
}

const STAGES: [&str; 5] = ["lex", "parse", "check", "emit", "write"];

/// The cases with a file of this extension beside them.
fn cases(extension: &str) -> Vec<PathBuf> {
    let found: Vec<_> = case_files()
        .into_iter()
        .filter(|p| p.with_extension(extension).exists())
        .collect();
    assert!(!found.is_empty());
    found
}

fn timings_list_every_stage_in_order_and_change_nothing_else() {
    for source in cases("out") {
        let name = source.file_stem().unwrap().to_str().unwrap();
        let file = format!("{name}.smith");
        let plain = folder("plain");
        let timed = folder("timed");
        for dir in [&plain, &timed] {
            std::fs::copy(&source, dir.join(&file)).unwrap();
        }
        let target = any().name();
        let without = smith(&plain, &["build", "--target", target, &file]);
        let with = smith(&timed, &["build", "--timings", "--target", target, &file]);
        assert!(without.status.success() && with.status.success(), "{file}");
        assert!(without.stderr.is_empty(), "{file}: {}", stderr(&without));
        assert!(with.stdout.is_empty(), "{file}");
        assert_eq!(
            std::fs::read(plain.join(name)).unwrap(),
            std::fs::read(timed.join(name)).unwrap(),
            "{file}"
        );
        let lines = timing_lines(&stderr(&with));
        let names: Vec<&str> = lines.iter().map(|(n, _)| n.as_str()).collect();
        let mut expected = STAGES.to_vec();
        expected.push("total");
        assert_eq!(names, expected, "{file}");
        let (_, total) = lines.last().unwrap();
        assert!(lines.iter().all(|(_, t)| t <= total), "{file}: {lines:?}");
    }
}

fn a_failed_build_lists_the_stages_that_ran_after_its_errors() {
    for source in cases("err") {
        let name = source.file_stem().unwrap().to_str().unwrap();
        let file = format!("{name}.smith");
        let dir = folder(name);
        std::fs::copy(&source, dir.join(&file)).unwrap();
        let built = smith(
            &dir,
            &["build", "--timings", "--target", any().name(), &file],
        );
        assert!(!built.status.success(), "{file}");
        let text = stderr(&built);
        let errors = std::fs::read_to_string(source.with_extension("err")).unwrap();
        assert!(text.starts_with(&errors), "{file}: {text}");
        let lines = timing_lines(&text[errors.len()..]);
        let names: Vec<&str> = lines.iter().map(|(n, _)| n.as_str()).collect();
        let (last, ran) = names.split_last().unwrap();
        assert_eq!(*last, "total", "{file}");
        assert!(!ran.is_empty() && ran.len() < STAGES.len(), "{file}");
        assert_eq!(ran, &STAGES[..ran.len()], "{file}");
        assert!(!dir.join(name).exists(), "{file}");
    }
}

fn run_prints_the_timings_before_the_program_runs() {
    if !Target::host().is_some_and(|host| targets::runnable().contains(&host)) {
        return;
    }
    let dir = folder("run_timings");
    std::fs::write(dir.join("hello.smith"), HELLO).unwrap();
    let ran = Command::new("sh")
        .args(["-c", "\"$0\" run --timings hello.smith 2>&1"])
        .arg(env!("CARGO_BIN_EXE_smith"))
        .current_dir(&dir)
        .output()
        .unwrap();
    assert_eq!(ran.status.code(), Some(0));
    let text = String::from_utf8(ran.stdout).unwrap();
    let output = text
        .strip_suffix("hello world\n")
        .expect("the program ran last");
    let names: Vec<_> = timing_lines(output).into_iter().map(|(n, _)| n).collect();
    assert_eq!(names.last().map(String::as_str), Some("total"));
}
