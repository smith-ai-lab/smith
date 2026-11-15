//! The tests every target passes. They take the targets from
//! `Target::ALL`, the enum's own list of cases, so adding a case runs them
//! over it, and no test names a target.
//!
//! `MACHINE` holds the tests whose result depends on the kind of machine
//! they run on. Continuous integration runs them on each kind of machine a
//! target needs, and every other test once.

use std::path::{Path, PathBuf};

use smith::eval::{self, Outcome};
use smith::target::Target;
use smith::{compile, lower};

use crate::Test;
use crate::generate;
use crate::programs;
use crate::sha256;

pub const ONCE: &[Test] = &[
    (
        "every_target_builds_identical_bytes_twice",
        every_target_builds_identical_bytes_twice,
    ),
    ("no_test_names_a_target", no_test_names_a_target),
    ("the_kept_hashes_are_sha256", the_kept_hashes_are_sha256),
];

pub const MACHINE: &[Test] = &[
    (
        "every_executable_does_what_the_evaluator_says",
        every_executable_does_what_the_evaluator_says,
    ),
    (
        "a_long_literal_is_printed_whole",
        a_long_literal_is_printed_whole,
    ),
    (
        "every_target_builds_the_cases_to_their_kept_hashes",
        every_target_builds_the_cases_to_their_kept_hashes,
    ),
];

const HELLO: &str = "fn main() ! console { print(\"hello world\") }\n";

/// The targets the compiler can build for, in the order of the enum.
pub fn buildable() -> Vec<Target> {
    let program = lower(HELLO.as_bytes()).unwrap();
    Target::ALL
        .iter()
        .copied()
        .filter(|t| t.emit(&program).is_some())
        .collect()
}

/// A target the compiler can build for, for a test that needs one.
pub fn any() -> Target {
    buildable()[0]
}

/// The targets the compiler can build for that this machine can run.
pub fn runnable() -> Vec<Target> {
    buildable()
        .into_iter()
        .filter(|t| Target::host() == Some(*t))
        .collect()
}

/// Each target this machine does not run, with why.
pub fn not_run() -> Vec<String> {
    let buildable = buildable();
    Target::ALL
        .iter()
        .filter(|t| !runnable().contains(t))
        .map(|t| {
            let why = if buildable.contains(t) {
                format!("this machine is {}", Target::host_description())
            } else {
                "the compiler cannot build for it yet".to_string()
            };
            format!("{}: not run, as {why}", t.name())
        })
        .collect()
}

/// Runs an executable for this machine and gives what it did.
pub fn run(bytes: &[u8]) -> Outcome {
    use std::sync::atomic::{AtomicUsize, Ordering};
    static COUNT: AtomicUsize = AtomicUsize::new(0);
    let dir = std::env::temp_dir().join(format!(
        "smith-run-{}-{}",
        std::process::id(),
        COUNT.fetch_add(1, Ordering::Relaxed)
    ));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("program");
    std::fs::write(&path, bytes).unwrap();
    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o755)).unwrap();
    }
    // Another test's process may briefly hold the file open while it
    // starts, which makes running it fail as busy.
    let mut tries = 0;
    let output = loop {
        match std::process::Command::new(&path).output() {
            Err(e) if e.raw_os_error() == Some(26) && tries < 100 => {
                tries += 1;
                std::thread::sleep(std::time::Duration::from_millis(10));
            }
            result => break result.unwrap(),
        }
    };
    std::fs::remove_dir_all(&dir).unwrap();
    Outcome {
        stdout: output.stdout,
        status: output.status.code().unwrap_or(-1),
    }
}

fn every_target_builds_identical_bytes_twice() {
    let targets = buildable();
    assert!(!targets.is_empty(), "the compiler builds for some target");
    generate::check(6, 100, programs::program, |case| {
        for &target in &targets {
            let first = compile(&case.source, target).unwrap();
            let second = compile(&case.source, target).unwrap();
            assert!(first == second, "{} differs", target.name());
        }
    });
}

fn every_executable_does_what_the_evaluator_says() {
    let targets = runnable();
    generate::check(3, 150, programs::program, |case| {
        let program = lower(&case.source).unwrap_or_else(|e| panic!("{e:?}"));
        let said = eval::run(&program);
        for &target in &targets {
            let bytes = compile(&case.source, target).unwrap_or_else(|e| panic!("{e:?}"));
            assert_eq!(run(&bytes), said, "{}", target.name());
        }
    });
}

fn a_long_literal_is_printed_whole() {
    let text = "abcdefghij".repeat(100_000);
    let source = format!("fn main() ! console {{ print(\"{text}\") }}\n");
    let program = lower(source.as_bytes()).unwrap();
    let said = eval::run(&program);
    assert_eq!(said.stdout, format!("{text}\n").into_bytes());
    for target in runnable() {
        let bytes = compile(source.as_bytes(), target).unwrap();
        assert_eq!(run(&bytes), said, "{}", target.name());
    }
}

/// The `.smith` files in `tests/cases`, in order.
pub fn case_files() -> Vec<PathBuf> {
    let cases = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/cases");
    let mut found: Vec<_> = std::fs::read_dir(&cases)
        .unwrap()
        .map(|e| e.unwrap().path())
        .filter(|p| p.extension().is_some_and(|e| e == "smith"))
        .collect();
    found.sort();
    assert!(!found.is_empty());
    found
}

/// Where the hashes of the cases' executables are kept.
fn hashes_file() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("tests/hashes")
}

/// Whether this run rewrites the expected output of the cases and their
/// hashes, as `SMITH_UPDATE=1 cargo test cases` asks.
pub fn updating() -> bool {
    std::env::var_os("SMITH_UPDATE").is_some_and(|v| !v.is_empty())
}

/// One line for each target and each case that builds for it: the target,
/// the case's file, and the SHA-256 of its executable.
fn hashes() -> String {
    let mut text = String::new();
    for target in buildable() {
        for source in case_files() {
            if let Ok(bytes) = compile(&std::fs::read(&source).unwrap(), target) {
                let file = source.file_name().unwrap().to_str().unwrap();
                let hash = sha256::hex(&bytes);
                text.push_str(&format!("{} {file} {hash}\n", target.name()));
            }
        }
    }
    text
}

fn every_target_builds_the_cases_to_their_kept_hashes() {
    let built = hashes();
    if updating() {
        std::fs::write(hashes_file(), &built).unwrap();
        return;
    }
    let kept = std::fs::read_to_string(hashes_file()).unwrap_or_default();
    let missing: Vec<&str> = built.lines().filter(|l| !kept.contains(l)).collect();
    let stale: Vec<&str> = kept.lines().filter(|l| !built.contains(l)).collect();
    assert!(
        missing.is_empty() && stale.is_empty(),
        "on {}, the executables differ from tests/hashes.\nbuilt now:\n{}\nkept:\n{}\n\
         if the change is meant, rewrite the file with `SMITH_UPDATE=1 cargo test cases`",
        Target::host_description(),
        missing.join("\n"),
        stale.join("\n"),
    );
}

fn the_kept_hashes_are_sha256() {
    // Two of the examples FIPS 180-4 gives.
    assert_eq!(
        sha256::hex(b"abc"),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    );
    assert_eq!(
        sha256::hex(b"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"
    );
}

/// The tests' own source files.
fn test_files() -> Vec<(PathBuf, String)> {
    let dir = Path::new(env!("CARGO_MANIFEST_DIR")).join("tests");
    let mut found: Vec<_> = std::fs::read_dir(&dir)
        .unwrap()
        .map(|e| e.unwrap().path())
        .filter(|p| p.extension().is_some_and(|e| e == "rs"))
        .map(|p| {
            let text = std::fs::read_to_string(&p).unwrap();
            (p, text)
        })
        .collect();
    found.sort();
    assert!(found.len() > 1);
    found
}

fn no_test_names_a_target() {
    for (file, text) in test_files() {
        for target in Target::ALL {
            for name in [target.name().to_string(), format!("Target::{target:?}")] {
                assert!(
                    !text.contains(&name),
                    "{} names {name}; take targets from Target::ALL",
                    file.display()
                );
            }
        }
    }
}
