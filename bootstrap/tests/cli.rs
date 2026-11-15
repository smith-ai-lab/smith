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
