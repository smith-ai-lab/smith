//! How fast the compiler builds. For hello, for the lexer in
//! `compiler/lexer.smith`, and for that lexer repeated under other function
//! names to 10 and to 100 times its size, it prints the time of `smith build`,
//! the lines for each second, and the time of each stage from `--timings`.
//! Then it builds the Rust lexer alone, unoptimized and with `-O`, and prints
//! beside the Smith lexer the time to build each, the size of each executable,
//! and the time each takes for a byte of the same input. When `valgrind` is
//! installed it also prints the instructions each takes for a byte, and the
//! instructions of each build, which do not vary from run to run as times do.

#[path = "../../bootstrap/tests/support/repeat.rs"]
mod repeat;

use std::ffi::OsStr;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

use crate::{RUNS, lex_at, lexer_input};

/// The size of the input of the lexers that are run, in bytes.
const MEGABYTE: usize = 1 << 20;

/// The size of the input the lexers run under `valgrind`, which is about fifty
/// times slower than a run.
const UNDER_VALGRIND: usize = 1 << 16;

/// The builds of the Smith lexer, by how many times its size.
const COPIES: [usize; 3] = [1, 10, 100];

pub fn run(smith: &Path, work: &Path, root: &Path) {
    let file = std::fs::read_to_string(root.join("compiler/lexer.smith")).unwrap();
    let lexer = repeat::lexer(&file);
    let hello = std::fs::read_to_string(root.join("bootstrap/tests/cases/hello.smith")).unwrap();
    let mut programs = vec![("hello".to_string(), hello)];
    for copies in COPIES {
        let name = if copies == 1 {
            "lexer".to_string()
        } else {
            format!("lexer_x{copies}")
        };
        programs.push((name, lexer_repeated(lexer, copies)));
    }
    for (name, source) in &programs {
        let built = build(smith, work, name, source);
        let lines = source.lines().count();
        let stages: Vec<String> = built
            .stages
            .iter()
            .map(|(stage, ms)| format!("{stage}_ms={ms:.2}"))
            .collect();
        println!(
            "build {name} lines={lines} total_ms={:.2} lines_per_s={:.0} {}",
            built.total.as_secs_f64() * 1000.0,
            lines as f64 / built.total.as_secs_f64(),
            stages.join(" ")
        );
        let file = work.join(format!("{name}.smith"));
        if let Some(ir) = instructions(
            &[smith.as_os_str(), "build".as_ref(), file.as_os_str()],
            None,
            work,
        ) {
            println!(
                "build {name} instructions={ir} instructions_per_line={}",
                ir / lines as u64
            );
        }
    }
    compare_lexers(smith, work, root, lexer);
}

/// The whole lexer repeated, with each copy lexing a few bytes.
fn lexer_repeated(lexer: &str, copies: usize) -> String {
    repeat::repeated(
        lexer,
        copies,
        "lex",
        "match lex(['f', 'n', ' ', 'm', '\\n']) {\n    .ok(tokens) { tokens.size }\n    .err(errors) { 0 }\n  }",
    )
}

/// The least of `RUNS` runs of `smith build` of a program, and the time of each
/// stage in that run.
struct Built {
    total: Duration,
    stages: Vec<(String, f64)>,
}

fn build(smith: &Path, work: &Path, name: &str, source: &str) -> Built {
    let file = work.join(format!("{name}.smith"));
    std::fs::write(&file, source).unwrap();
    let mut best: Option<Built> = None;
    for _ in 0..RUNS {
        let start = Instant::now();
        let out = Command::new(smith)
            .args(["build", "--timings"])
            .arg(&file)
            .current_dir(work)
            .output()
            .unwrap();
        let total = start.elapsed();
        assert!(
            out.status.success(),
            "smith build {} failed",
            file.display()
        );
        if best.as_ref().is_none_or(|b| total < b.total) {
            let stderr = String::from_utf8_lossy(&out.stderr);
            let stages = stderr
                .lines()
                .filter_map(|l| {
                    let (stage, ms) = l.trim_end().strip_suffix(" ms")?.rsplit_once(' ')?;
                    Some((stage.trim().to_string(), ms.parse().ok()?))
                })
                .filter(|(stage, _)| stage != "total")
                .collect();
            best = Some(Built { total, stages });
        }
    }
    best.unwrap()
}

/// The Smith lexer beside the Rust lexer built alone, as a program that reads
/// its input and prints the number of tokens.
fn compare_lexers(smith: &Path, work: &Path, root: &Path, lexer: &str) {
    let bootstrap = root.join("bootstrap/src").canonicalize().unwrap();
    let main = work.join("rust_lexer.rs");
    std::fs::write(
        &main,
        format!(
            "#![allow(dead_code)]\n#[path = {diag:?}]\nmod diag;\n#[path = {lexer:?}]\nmod lexer;\nfn library_text(_: &str) -> &'static str {{ \"\" }}\nfn main() {{\n    use std::io::Read;\n    let mut input = Vec::new();\n    std::io::stdin().read_to_end(&mut input).unwrap();\n    match lexer::lex(&input) {{\n        Ok(tokens) => println!(\"{{}}\", tokens.len()),\n        Err(_) => println!(\"errors\"),\n    }}\n}}\n",
            diag = bootstrap.join("diag.rs"),
            lexer = bootstrap.join("lexer.rs"),
        ),
    )
    .unwrap();
    let input_file = work.join("lexer_input");
    std::fs::write(&input_file, lexer_input(MEGABYTE).1).unwrap();
    let small_file = work.join("lexer_input_small");
    std::fs::write(&small_file, lexer_input(UNDER_VALGRIND).1).unwrap();

    // The Smith lexer, built from the program `lex_at` times.
    let lexer_exe = work.join("lexer");
    let built = build(smith, work, "lexer", &lexer_repeated(lexer, 1));
    let (bytes, _, time, _) = lex_at(smith, work, lexer, MEGABYTE);
    println!(
        "lexers smith build_ms={:.2} bytes={} ns_per_byte={}",
        built.total.as_secs_f64() * 1000.0,
        size(&lexer_exe),
        per_byte(time, bytes),
    );
    for (who, flags) in [("rust", &[][..]), ("rust_O", &["-O"][..])] {
        let exe = work.join(format!("rust_lexer_{who}"));
        let mut best: Option<Duration> = None;
        for _ in 0..RUNS {
            let start = Instant::now();
            let status = Command::new("rustc")
                .args(["--edition", "2024", "-o"])
                .arg(&exe)
                .args(flags)
                .arg(&main)
                .stderr(Stdio::null())
                .status()
                .expect("rustc");
            assert!(status.success(), "rustc failed on the Rust lexer");
            let took = start.elapsed();
            best = Some(best.map_or(took, |b| b.min(took)));
        }
        let run = least_with_stdin(&exe, &input_file);
        println!(
            "lexers {who} build_ms={:.2} bytes={} ns_per_byte={} (with the start of the process and its read)",
            best.unwrap().as_secs_f64() * 1000.0,
            size(&exe),
            per_byte(run, bytes),
        );
    }

    // Instructions for a byte, when valgrind is there.
    let (small, _, _, rounds) = lex_at(smith, work, lexer, UNDER_VALGRIND);
    let run = |exe: PathBuf, stdin: Option<&Path>| instructions(&[exe.as_os_str()], stdin, work);
    let smith_ir = run(work.join("lexer-whole"), None)
        .zip(run(work.join("lexer-base"), None))
        .map(|(whole, base)| (whole.saturating_sub(base) / rounds as u64, "smith"));
    let rust_ir = run(work.join("rust_lexer_rust"), Some(&small_file)).map(|ir| (ir, "rust"));
    let rust_o_ir = run(work.join("rust_lexer_rust_O"), Some(&small_file)).map(|ir| (ir, "rust_O"));
    for (ir, who) in [smith_ir, rust_ir, rust_o_ir].into_iter().flatten() {
        println!(
            "lexers {who} instructions_per_byte={:.1} (of {small} bytes, under valgrind)",
            ir as f64 / small as f64
        );
    }
}

fn size(exe: &Path) -> u64 {
    std::fs::metadata(exe).map(|m| m.len()).unwrap_or(0)
}

fn per_byte(time: impl Into<Option<Duration>>, bytes: usize) -> String {
    match time.into() {
        Some(t) => format!("{:.1}", t.as_nanos() as f64 / bytes as f64),
        None => "gave up".to_string(),
    }
}

/// The least of `RUNS` runs of an executable that reads a file from its
/// standard input.
fn least_with_stdin(exe: &Path, input: &Path) -> Option<Duration> {
    let mut best: Option<Duration> = None;
    for _ in 0..RUNS {
        let start = Instant::now();
        let status = Command::new(exe)
            .stdin(std::fs::File::open(input).unwrap())
            .stdout(Stdio::null())
            .status()
            .ok()?;
        if !status.success() {
            return None;
        }
        let took = start.elapsed();
        best = Some(best.map_or(took, |b| b.min(took)));
    }
    best
}

/// How many instructions a program runs, from `valgrind --tool=callgrind`, or
/// `None` when valgrind is not installed or the run fails. The program is the
/// first of `command`, and the rest are its arguments, in the folder `work`,
/// with `stdin` for its input. The count is the same on every run, which a time
/// is not.
fn instructions(command: &[&OsStr], stdin: Option<&Path>, work: &Path) -> Option<u64> {
    let mut valgrind = Command::new("valgrind");
    valgrind
        .arg("--tool=callgrind")
        .arg(format!(
            "--callgrind-out-file={}",
            work.join("callgrind.out").display()
        ))
        .args(command)
        .current_dir(work)
        .stdout(Stdio::null());
    if let Some(file) = stdin {
        valgrind.stdin(std::fs::File::open(file).ok()?);
    }
    let out = valgrind.output().ok()?;
    String::from_utf8_lossy(&out.stderr)
        .lines()
        .find_map(|l| l.split("Collected : ").nth(1))
        .and_then(|n| n.split_whitespace().next()?.parse().ok())
}
