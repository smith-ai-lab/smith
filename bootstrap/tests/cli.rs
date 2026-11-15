//! The `smith build` and `smith run` commands, and the cases in `cases/`.
//!
//! A case is a `.smith` file with either a `.out` file beside it, holding
//! what the program prints, or a `.err` file, holding the error that
//! building it reports.

use std::path::{Path, PathBuf};
use std::process::{Command, Output};
use std::sync::atomic::{AtomicUsize, Ordering};

const HELLO: &str = "fn main() ! console { print(\"hello world\") }\n";

/// Whether this machine can run what `linux_x64` builds.
const NATIVE: bool = cfg!(all(target_os = "linux", target_arch = "x86_64"));

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

#[test]
fn the_cases_build_and_print_or_report_their_error() {
    let cases = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/cases");
    let mut entries: Vec<_> = std::fs::read_dir(&cases)
        .unwrap()
        .map(|e| e.unwrap().path())
        .filter(|p| p.extension().is_some_and(|e| e == "smith"))
        .collect();
    entries.sort();
    assert!(!entries.is_empty());
    for source in entries {
        let name = source.file_stem().unwrap().to_str().unwrap();
        let file = format!("{name}.smith");
        let dir = folder(name);
        std::fs::copy(&source, dir.join(&file)).unwrap();
        let out = source.with_extension("out");
        let err = source.with_extension("err");
        if out.exists() {
            let built = smith(&dir, &["build", "--target", "linux_x64", &file]);
            assert!(built.status.success(), "{file}: {}", stderr(&built));
            assert!(dir.join(name).is_file(), "{file} writes {name}");
            if NATIVE {
                let ran = smith(&dir, &["run", &file]);
                assert_eq!(ran.status.code(), Some(0), "{file}: {}", stderr(&ran));
                assert_eq!(ran.stdout, std::fs::read(&out).unwrap(), "{file}");
            }
        } else {
            let built = smith(&dir, &["build", "--target", "linux_x64", &file]);
            assert!(!built.status.success(), "{file} is rejected");
            let expected = std::fs::read_to_string(&err).unwrap();
            assert_eq!(stderr(&built), expected, "{file}");
            assert!(!dir.join(name).exists(), "{file} writes no executable");
        }
    }
}

#[test]
fn build_writes_the_executable_to_the_folder_it_ran_in() {
    let dir = folder("work");
    std::fs::create_dir_all(dir.join("src")).unwrap();
    std::fs::write(dir.join("src/hello.smith"), HELLO).unwrap();
    std::fs::write(dir.join("hello"), "an old file").unwrap();
    let built = smith(&dir, &["build", "--target", "linux_x64", "src/hello.smith"]);
    assert!(built.status.success(), "{}", stderr(&built));
    assert!(!dir.join("src/hello").exists());
    if NATIVE {
        let ran = Command::new(dir.join("hello")).output().unwrap();
        assert_eq!(ran.status.code(), Some(0));
        assert_eq!(ran.stdout, b"hello world\n");
        assert!(ran.stderr.is_empty());
    }
}

#[test]
fn building_twice_gives_identical_bytes() {
    let first = folder("first");
    let second = folder("second");
    for dir in [&first, &second] {
        std::fs::write(dir.join("hello.smith"), HELLO).unwrap();
        let built = smith(dir, &["build", "--target", "linux_x64", "hello.smith"]);
        assert!(built.status.success(), "{}", stderr(&built));
    }
    let bytes = std::fs::read(first.join("hello")).unwrap();
    assert_eq!(bytes, std::fs::read(second.join("hello")).unwrap());
    let again = smith(&first, &["build", "--target", "linux_x64", "hello.smith"]);
    assert!(again.status.success());
    assert_eq!(bytes, std::fs::read(first.join("hello")).unwrap());
}

#[test]
fn main_smith_is_the_entry_and_takes_the_folder_name() {
    let dir = folder("greeter");
    std::fs::write(dir.join("main.smith"), HELLO).unwrap();
    let built = smith(&dir, &["build", "--target", "linux_x64"]);
    assert!(built.status.success(), "{}", stderr(&built));
    assert!(dir.join("greeter").is_file());
}

#[test]
fn without_a_flag_the_target_is_this_machine() {
    let dir = folder("host");
    std::fs::write(dir.join("hello.smith"), HELLO).unwrap();
    let built = smith(&dir, &["build", "hello.smith"]);
    if NATIVE {
        assert!(built.status.success(), "{}", stderr(&built));
        let ran = smith(&dir, &["run", "hello.smith"]);
        assert_eq!(ran.status.code(), Some(0));
        assert_eq!(ran.stdout, b"hello world\n");
    } else if !cfg!(all(target_os = "macos", target_arch = "aarch64")) {
        assert!(stderr(&built).contains("is not a target"));
    }
}

#[test]
fn an_unknown_target_is_an_error_that_names_the_targets() {
    let dir = folder("unknown");
    std::fs::write(dir.join("hello.smith"), HELLO).unwrap();
    let built = smith(&dir, &["build", "--target", "vax", "hello.smith"]);
    assert!(!built.status.success());
    assert!(stderr(&built).contains("linux_x64"), "{}", stderr(&built));
}

#[test]
fn run_rejects_a_target_other_than_this_machine() {
    let dir = folder("other");
    std::fs::write(dir.join("hello.smith"), HELLO).unwrap();
    let other = if NATIVE { "macos_arm64" } else { "linux_x64" };
    let ran = smith(&dir, &["run", "--target", other, "hello.smith"]);
    assert!(!ran.status.success());
    assert!(stderr(&ran).contains(other), "{}", stderr(&ran));
    assert!(!dir.join("hello").exists());
}

#[test]
fn a_file_must_be_named_smith() {
    let dir = folder("named");
    std::fs::write(dir.join("hello.txt"), HELLO).unwrap();
    let built = smith(&dir, &["build", "--target", "linux_x64", "hello.txt"]);
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

fn cases(extension: &str) -> Vec<PathBuf> {
    let cases = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/cases");
    let mut found: Vec<_> = std::fs::read_dir(&cases)
        .unwrap()
        .map(|e| e.unwrap().path())
        .filter(|p| p.extension().is_some_and(|e| e == "smith"))
        .filter(|p| p.with_extension(extension).exists())
        .collect();
    found.sort();
    assert!(!found.is_empty());
    found
}

#[test]
fn timings_list_every_stage_in_order_and_change_nothing_else() {
    for source in cases("out") {
        let name = source.file_stem().unwrap().to_str().unwrap();
        let file = format!("{name}.smith");
        let plain = folder("plain");
        let timed = folder("timed");
        for dir in [&plain, &timed] {
            std::fs::copy(&source, dir.join(&file)).unwrap();
        }
        let without = smith(&plain, &["build", "--target", "linux_x64", &file]);
        let with = smith(
            &timed,
            &["build", "--timings", "--target", "linux_x64", &file],
        );
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

#[test]
fn a_failed_build_lists_the_stages_that_ran_after_its_errors() {
    for source in cases("err") {
        let name = source.file_stem().unwrap().to_str().unwrap();
        let file = format!("{name}.smith");
        let dir = folder(name);
        std::fs::copy(&source, dir.join(&file)).unwrap();
        let built = smith(
            &dir,
            &["build", "--timings", "--target", "linux_x64", &file],
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

#[test]
fn run_prints_the_timings_before_the_program_runs() {
    if !NATIVE {
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
