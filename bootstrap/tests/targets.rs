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
use crate::enums;
use crate::generate;
use crate::lambdas;
use crate::programs;
use crate::reprs;
use crate::sha256;
use crate::text;
use crate::traits;
use crate::tries;
use crate::tuples;

pub const ONCE: &[Test] = &[
    (
        "every_target_builds_identical_bytes_twice",
        every_target_builds_identical_bytes_twice,
    ),
    ("no_test_names_a_target", no_test_names_a_target),
    ("the_kept_hashes_are_sha256", the_kept_hashes_are_sha256),
    (
        "every_executable_uses_only_thin_wrappers_over_the_kernel",
        every_executable_uses_only_thin_wrappers_over_the_kernel,
    ),
];

pub const MACHINE: &[Test] = &[
    (
        "every_executable_does_what_the_evaluator_says",
        every_executable_does_what_the_evaluator_says,
    ),
    (
        "every_executable_of_a_program_with_traits_does_what_the_evaluator_says",
        every_executable_of_a_program_with_traits_does_what_the_evaluator_says,
    ),
    (
        "every_executable_of_a_program_with_enums_does_what_the_evaluator_says",
        every_executable_of_a_program_with_enums_does_what_the_evaluator_says,
    ),
    (
        "every_executable_of_a_program_with_tuples_does_what_the_evaluator_says",
        every_executable_of_a_program_with_tuples_does_what_the_evaluator_says,
    ),
    (
        "every_executable_of_a_program_with_lambdas_does_what_the_evaluator_says",
        every_executable_of_a_program_with_lambdas_does_what_the_evaluator_says,
    ),
    (
        "every_executable_of_a_program_with_with_and_try_does_what_the_evaluator_says",
        every_executable_of_a_program_with_with_and_try_does_what_the_evaluator_says,
    ),
    (
        "a_struct_bound_to_two_names_is_two_equal_structs",
        a_struct_bound_to_two_names_is_two_equal_structs,
    ),
    (
        "a_long_literal_is_printed_whole",
        a_long_literal_is_printed_whole,
    ),
    (
        "every_executable_of_a_program_with_text_does_what_the_evaluator_says",
        every_executable_of_a_program_with_text_does_what_the_evaluator_says,
    ),
    (
        "every_executable_of_a_program_with_reprs_does_what_the_evaluator_says",
        every_executable_of_a_program_with_reprs_does_what_the_evaluator_says,
    ),
    (
        "a_program_the_system_refuses_memory_ends_with_a_failing_status_and_does_nothing_more",
        a_program_the_system_refuses_memory_ends_with_a_failing_status_and_does_nothing_more,
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
    run_with(bytes, |path| std::process::Command::new(path))
}

/// Runs an executable for this machine with the command `command` makes of
/// its path, and gives what it did.
fn run_with(bytes: &[u8], command: impl Fn(&Path) -> std::process::Command) -> Outcome {
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
        match command(&path).output() {
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
    generate::check(6, 15, programs::program, |case| {
        for &target in &targets {
            let first = compile(&case.source, target).unwrap();
            let second = compile(&case.source, target).unwrap();
            assert!(first == second, "{} differs", target.name());
        }
    });
    generate::check(19, 10, traits::program, |case| {
        for &target in &targets {
            let first = compile(case.source.as_bytes(), target).unwrap();
            let second = compile(case.source.as_bytes(), target).unwrap();
            assert!(first == second, "{} differs", target.name());
        }
    });
    generate::check(24, 10, enums::program, |case| {
        for &target in &targets {
            let first = compile(case.source.as_bytes(), target).unwrap();
            let second = compile(case.source.as_bytes(), target).unwrap();
            assert!(first == second, "{} differs", target.name());
        }
    });
    generate::check(36, 10, text::program, |case| {
        for &target in &targets {
            let first = compile(case.source.as_bytes(), target).unwrap();
            let second = compile(case.source.as_bytes(), target).unwrap();
            assert!(first == second, "{} differs", target.name());
        }
    });
    generate::check(65, 10, reprs::program, |case| {
        for &target in &targets {
            let first = compile(case.source.as_bytes(), target).unwrap();
            let second = compile(case.source.as_bytes(), target).unwrap();
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

fn every_executable_of_a_program_with_traits_does_what_the_evaluator_says() {
    let targets = runnable();
    generate::check(18, 60, traits::program, |case| {
        let source = case.source.as_bytes();
        let program = lower(source).unwrap_or_else(|e| panic!("{e:?}"));
        let said = eval::run(&program);
        for &target in &targets {
            let bytes = compile(source, target).unwrap_or_else(|e| panic!("{e:?}"));
            assert_eq!(run(&bytes), said, "{}", target.name());
        }
    });
}

fn every_executable_of_a_program_with_enums_does_what_the_evaluator_says() {
    let targets = runnable();
    generate::check(25, 60, enums::program, |case| {
        let source = case.source.as_bytes();
        let program = lower(source).unwrap_or_else(|e| panic!("{e:?}"));
        let said = eval::run(&program);
        for &target in &targets {
            let bytes = compile(source, target).unwrap_or_else(|e| panic!("{e:?}"));
            assert_eq!(run(&bytes), said, "{}", target.name());
        }
    });
}

fn every_executable_of_a_program_with_tuples_does_what_the_evaluator_says() {
    let targets = runnable();
    generate::check(28, 60, tuples::program, |case| {
        let source = case.source.as_bytes();
        let program = lower(source).unwrap_or_else(|e| panic!("{e:?}"));
        let said = eval::run(&program);
        for &target in &targets {
            let bytes = compile(source, target).unwrap_or_else(|e| panic!("{e:?}"));
            assert_eq!(run(&bytes), said, "{}", target.name());
        }
    });
}

fn every_executable_of_a_program_with_lambdas_does_what_the_evaluator_says() {
    let targets = runnable();
    generate::check(32, 60, lambdas::program, |case| {
        let source = case.source.as_bytes();
        let program = lower(source).unwrap_or_else(|e| panic!("{e:?}"));
        let said = eval::run(&program);
        for &target in &targets {
            let bytes = compile(source, target).unwrap_or_else(|e| panic!("{e:?}"));
            assert_eq!(run(&bytes), said, "{}", target.name());
        }
    });
}

fn every_executable_of_a_program_with_with_and_try_does_what_the_evaluator_says() {
    let targets = runnable();
    generate::check(43, 60, tries::program, |case| {
        let source = case.source.as_bytes();
        let program = lower(source).unwrap_or_else(|e| panic!("{e:?}"));
        let said = eval::run(&program);
        for &target in &targets {
            let bytes = compile(source, target).unwrap_or_else(|e| panic!("{e:?}"));
            assert_eq!(run(&bytes), said, "{}", target.name());
        }
    });
}

fn a_struct_bound_to_two_names_is_two_equal_structs() {
    let targets = runnable();
    generate::check(13, 100, programs::shared_and_copied, |(shared, copied)| {
        let said = eval::run(&lower(shared.as_bytes()).unwrap_or_else(|e| panic!("{e:?}")));
        let copy = eval::run(&lower(copied.as_bytes()).unwrap_or_else(|e| panic!("{e:?}")));
        assert_eq!(said, copy);
        for &target in &targets {
            let bytes = compile(shared.as_bytes(), target).unwrap_or_else(|e| panic!("{e:?}"));
            assert_eq!(run(&bytes), copy, "{}", target.name());
        }
    });
}

fn every_executable_of_a_program_with_text_does_what_the_evaluator_says() {
    let targets = runnable();
    generate::check(37, 60, text::program, |case| {
        let source = case.source.as_bytes();
        let program = lower(source).unwrap_or_else(|e| panic!("{e:?}"));
        let said = eval::run(&program);
        for &target in &targets {
            let bytes = compile(source, target).unwrap_or_else(|e| panic!("{e:?}"));
            assert_eq!(run(&bytes), said, "{}", target.name());
        }
    });
}

fn every_executable_of_a_program_with_reprs_does_what_the_evaluator_says() {
    let targets = runnable();
    generate::check(64, 60, reprs::program, |case| {
        let source = case.source.as_bytes();
        let program = lower(source).unwrap_or_else(|e| panic!("{e:?}"));
        let said = eval::run(&program);
        for &target in &targets {
            let bytes = compile(source, target).unwrap_or_else(|e| panic!("{e:?}"));
            assert_eq!(run(&bytes), said, "{}", target.name());
        }
    });
}

/// The most memory, in KiB, a program run under a limit may map.
const LIMIT_KIB: usize = 64 * 1024;

/// The command that runs the program at `path` with the memory it may map
/// limited to `LIMIT_KIB`, on a machine where a test can limit it. Only
/// Linux enforces the limit a shell sets.
fn limited(path: &Path) -> Option<std::process::Command> {
    if !cfg!(target_os = "linux") {
        return None;
    }
    let mut command = std::process::Command::new("sh");
    command
        .arg("-c")
        .arg(format!("ulimit -v {LIMIT_KIB} && exec \"$0\""))
        .arg(path);
    Some(command)
}

/// As `tasks.md` says, running out of memory ends the task, and a failure
/// that reaches `main` ends the program with a failing status. The text the
/// program builds is larger than the limit lets it map, and everything
/// before it fits, so the program prints what comes before and no more.
fn a_program_the_system_refuses_memory_ends_with_a_failing_status_and_does_nothing_more() {
    if limited(Path::new("")).is_none() {
        return;
    }
    let targets = runnable();
    let bytes = LIMIT_KIB * 1024 * 2;
    let generate = |g: &mut generate::Gen| text::program_with_huge_text(g, bytes);
    generate::check(38, 20, generate, |(case, before)| {
        let source = case.source.as_bytes();
        for &target in &targets {
            let executable = compile(source, target).unwrap_or_else(|e| panic!("{e:?}"));
            let ran = run_with(&executable, |path| limited(path).expect("a limit"));
            // A status of its own, not the end a signal gives.
            assert!(ran.status > 0, "{}: {}", target.name(), ran.status);
            assert_eq!(
                String::from_utf8_lossy(&ran.stdout),
                String::from_utf8_lossy(before),
                "{}",
                target.name()
            );
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

/// The functions of a system library that are thin wrappers over the
/// kernel, as "Standing alone" in `knowledge/compiler.md` allows, by the
/// names a Mach-O file gives them.
const THIN_WRAPPERS: &[&str] = &["_write", "__exit", "___error", "_mmap", "_munmap"];

/// The same, by the names `KERNEL32.dll` gives them.
const KERNEL32_THIN_WRAPPERS: &[&str] = &[
    "GetStdHandle",
    "WriteFile",
    "ExitProcess",
    "VirtualAlloc",
    "VirtualFree",
];

fn u16_at(bytes: &[u8], at: usize) -> usize {
    u16::from_le_bytes(bytes[at..at + 2].try_into().unwrap()) as usize
}

fn u32_at(bytes: &[u8], at: usize) -> usize {
    u32::from_le_bytes(bytes[at..at + 4].try_into().unwrap()) as usize
}

fn c_string(bytes: &[u8], at: usize) -> String {
    let end = bytes[at..].iter().position(|&b| b == 0).unwrap();
    String::from_utf8(bytes[at..at + end].to_vec()).unwrap()
}

/// The libraries a Mach-O executable loads, and the symbols it imports.
fn mach_o_imports(bytes: &[u8]) -> (Vec<String>, Vec<String>) {
    const LC_SYMTAB: usize = 0x2;
    const LC_LOAD_DYLIB: usize = 0xc;
    let mut libraries = Vec::new();
    let mut symbols = Vec::new();
    let mut at = 32;
    for _ in 0..u32_at(bytes, 16) {
        let (cmd, size) = (u32_at(bytes, at), u32_at(bytes, at + 4));
        if cmd == LC_LOAD_DYLIB {
            libraries.push(c_string(bytes, at + u32_at(bytes, at + 8)));
        } else if cmd == LC_SYMTAB {
            let (table, count) = (u32_at(bytes, at + 8), u32_at(bytes, at + 12));
            let strings = u32_at(bytes, at + 16);
            for i in 0..count {
                let entry = table + 16 * i;
                let kind = bytes[entry + 4];
                // External, and undefined: a symbol the loader finds.
                if kind & 0x01 != 0 && kind & 0x0e == 0 {
                    symbols.push(c_string(bytes, strings + u32_at(bytes, entry)));
                }
            }
        }
        at += size;
    }
    (libraries, symbols)
}

/// The libraries a PE executable loads, and the symbols it imports from
/// them.
fn pe_imports(bytes: &[u8]) -> (Vec<String>, Vec<String>) {
    let pe = u32_at(bytes, 0x3c);
    let sections = u16_at(bytes, pe + 6);
    let optional = pe + 24;
    let section_table = optional + u16_at(bytes, pe + 20);
    // Where an address in memory is in the file.
    let offset = |rva: usize| {
        (0..sections)
            .map(|i| section_table + 40 * i)
            .find_map(|h| {
                let (start, size) = (u32_at(bytes, h + 12), u32_at(bytes, h + 8));
                (start..start + size)
                    .contains(&rva)
                    .then(|| rva - start + u32_at(bytes, h + 20))
            })
            .expect("an address inside a section")
    };
    let mut libraries = Vec::new();
    let mut symbols = Vec::new();
    // The import directory, the second data directory of PE32+.
    let directory = u32_at(bytes, optional + 112 + 8);
    if directory == 0 {
        return (libraries, symbols);
    }
    let mut entry = offset(directory);
    while u32_at(bytes, entry + 12) != 0 {
        libraries.push(c_string(bytes, offset(u32_at(bytes, entry + 12))));
        let mut slot = offset(u32_at(bytes, entry));
        loop {
            let value = u64::from_le_bytes(bytes[slot..slot + 8].try_into().unwrap());
            if value == 0 {
                break;
            }
            assert!(value >> 63 == 0, "a symbol is imported by its name");
            symbols.push(c_string(bytes, offset(value as usize) + 2));
            slot += 8;
        }
        entry += 20;
    }
    (libraries, symbols)
}

/// The kinds of the segments an ELF executable's program headers list.
fn elf_segment_kinds(bytes: &[u8]) -> Vec<usize> {
    let headers = u32_at(bytes, 32);
    let size = u16_at(bytes, 54);
    (0..u16_at(bytes, 56))
        .map(|i| u32_at(bytes, headers + i * size))
        .collect()
}

/// A program that asks the system for memory.
const JOIN: &str = "fn main() ! console, alloc {\n  x = \"a\"\n  print(\"{x}{x}\")\n}\n";

fn every_executable_uses_only_thin_wrappers_over_the_kernel() {
    const PT_DYNAMIC: usize = 2;
    const PT_INTERP: usize = 3;
    let programs = [HELLO, JOIN];
    for (target, source) in buildable()
        .into_iter()
        .flat_map(|t| programs.map(|p| (t, p)))
    {
        let bytes = compile(source.as_bytes(), target).unwrap();
        let name = target.name();
        if bytes.starts_with(b"\x7fELF") {
            let kinds = elf_segment_kinds(&bytes);
            assert!(
                !kinds.contains(&PT_INTERP) && !kinds.contains(&PT_DYNAMIC),
                "{name} links a library"
            );
        } else if bytes.starts_with(&0xfeed_facfu32.to_le_bytes()) {
            let (libraries, symbols) = mach_o_imports(&bytes);
            assert_eq!(
                libraries,
                ["/usr/lib/libSystem.B.dylib"],
                "{name} links only libSystem"
            );
            assert!(!symbols.is_empty(), "{name} imports what it calls");
            for symbol in symbols {
                assert!(
                    THIN_WRAPPERS.contains(&symbol.as_str()),
                    "{name} imports {symbol}, which is not a thin wrapper over the kernel"
                );
            }
        } else if bytes.starts_with(b"MZ") {
            let (libraries, symbols) = pe_imports(&bytes);
            assert_eq!(libraries, ["KERNEL32.dll"], "{name} links only KERNEL32");
            assert!(!symbols.is_empty(), "{name} imports what it calls");
            for symbol in symbols {
                assert!(
                    KERNEL32_THIN_WRAPPERS.contains(&symbol.as_str()),
                    "{name} imports {symbol}, which is not a thin wrapper over the kernel"
                );
            }
        } else {
            panic!("{name} writes an executable in a form this test does not read");
        }
    }
}
