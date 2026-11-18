//! Times the two programs that decide how fast a Smith program is, a loop
//! that pushes to a list and the lexer in `compiler/lexer.smith`, and the Rust
//! lexer on the same inputs, and how fast the compiler builds. It prints one
//! line for each program, each implementation, and each size, with the time
//! for each push or each byte, and for `build` the time of each stage of a
//! build. Run it with `sh measure/run.sh`, which times the compiler in
//! `bootstrap/target/release`, or set `SMITH` to the path of another
//! compiler. It needs the compiler built in `bootstrap/target/release`, which
//! `run.sh` builds. Nothing here is a test.

mod build;

use std::hint::black_box;
use std::io::Read;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

/// The sizes, in pushes or in bytes: 1 KB to 1 MB.
const SIZES: [usize; 6] = [1 << 10, 1 << 12, 1 << 14, 1 << 16, 1 << 18, 1 << 20];

/// A run of an executable that takes longer is given up on.
const GIVE_UP: Duration = Duration::from_secs(30);

/// A run of a program is made at least this long, so that the time the system
/// takes to start it is small beside it.
const LONG_ENOUGH: Duration = Duration::from_millis(30);

/// How many times an executable is run, and the least is kept.
pub(crate) const RUNS: usize = 5;

/// One piece of Smith source, repeated to make the input of the lexer. It
/// holds names, keywords, numbers, a string, a comment, and punctuation.
const PIECE: &str =
    "// add two numbers\nfn add(a: u64, b: u64) -> u64 {\n  s = \"two\"\n  a + b * 0x1F - 3\n}\n";

fn main() {
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("..");
    let smith = std::env::var_os("SMITH")
        .map(PathBuf::from)
        .unwrap_or_else(|| root.join("bootstrap/target/release/smith"));
    let lexer_source =
        std::fs::read_to_string(root.join("compiler/lexer.smith")).expect("compiler/lexer.smith");
    let lexer_source = &lexer_source[..lexer_source
        .find("\nfn main()")
        .expect("lexer.smith has a main")
        + 1];
    let work = std::env::temp_dir().join(format!("smith-measure-{}", std::process::id()));
    std::fs::create_dir_all(&work).unwrap();

    let wanted: Vec<String> = std::env::args().skip(1).collect();
    let wants = |name: &str| wanted.is_empty() || wanted.iter().any(|w| w == name);
    if wants("push") {
        push(&smith, &work);
    }
    if wants("lexer") {
        lex(&smith, &work, lexer_source);
    }
    if wants("build") {
        build::run(&smith, &work, &root);
    }
    std::fs::remove_dir_all(&work).ok();
}

fn push(smith: &Path, work: &Path) {
    for n in SIZES {
        let program = |reps: usize, body: &str| {
            format!(
                "fn main() ! console, alloc {{\n  var last: Nat = 0\n  loop k in 0..{reps} {{\n    var xs: List[u64] = []\n    loop i in 0..{n} {{ {body} }}\n    last = xs.size\n  }}\n  print(\"{{last}}\")\n}}\n"
            )
        };
        let (time, reps) = smith_time(smith, work, "push", &n.to_string(), |reps| {
            (program(reps, "xs = push(xs, i)"), program(reps, ""))
        });
        let rust = per_run(|| {
            let mut xs: Vec<u64> = Vec::new();
            for i in 0..n as u64 {
                xs.push(black_box(i));
            }
            black_box(xs.len());
        });
        report("push", "smith", n, "push", reps, time);
        report("push", "rust", n, "push", 1, Some(rust));
    }
}

fn lex(smith: &Path, work: &Path, lexer_source: &str) {
    for size in SIZES {
        let (bytes, tokens, time, reps) = lex_at(smith, work, lexer_source, size);
        println!(
            "check lexer size={bytes} tokens={tokens} (the Smith lexer prints the same count)"
        );
        report("lexer", "smith", bytes, "byte", reps, time);
        let rust = per_run(|| {
            black_box(smith::count_tokens(black_box(&lexer_input(size).1)).ok());
        });
        report("lexer", "rust", bytes, "byte", 1, Some(rust));
    }
}

/// The input of the lexer of about `size` bytes: how many pieces it is, and
/// its bytes.
pub(crate) fn lexer_input(size: usize) -> (usize, Vec<u8>) {
    let pieces = size / PIECE.len();
    (pieces, PIECE.as_bytes().repeat(pieces))
}

/// The Smith lexer on an input of about `size` bytes: the bytes, the tokens
/// the Rust lexer finds in them, and the time of one lexing of them with the
/// rounds a run is of.
pub(crate) fn lex_at(
    smith: &Path,
    work: &Path,
    lexer_source: &str,
    size: usize,
) -> (usize, usize, Option<Duration>, usize) {
    let piece: Vec<String> = PIECE.bytes().map(|b| b.to_string()).collect();
    let piece = piece.join(", ");
    let (pieces, input) = lexer_input(size);
    let program = |reps: usize, body: &str| {
        format!(
            "{lexer_source}\nfn piece() -> List[u8] ! alloc {{\n  [{piece}]\n}}\n\nfn main() ! console, alloc {{\n  p = piece()\n  var last: Nat = 0\n  loop k in 0..{reps} {{\n    var src: List[u8] = []\n    loop r in 0..{pieces} {{\n      loop j in 0..p.size {{ src = push(src, p[j]) }}\n    }}\n    {body}\n  }}\n  print(\"{{last}}\")\n}}\n"
        )
    };
    let lexing = "match lex(src) {\n      .ok(tokens) { last = tokens.size }\n      .err(errors) { last = 0 }\n    }";
    let tokens = smith::count_tokens(&input).expect("the input lexes");
    let (time, reps) = smith_time(smith, work, "lexer", &tokens.to_string(), |reps| {
        (program(reps, lexing), program(reps, "last = src.size"))
    });
    (input.len(), tokens, time, reps)
}

/// How long one round of the program takes, and how many rounds a run is of.
/// A run is of one round, and of more when that is too short to tell from the
/// time the system takes to start it. `programs` gives the program and the
/// baseline for a number of rounds. The baseline builds the same input and
/// does nothing with it, and its time is taken away. The time is `None` when a
/// run does not finish.
pub(crate) fn smith_time(
    smith: &Path,
    work: &Path,
    name: &str,
    expected: &str,
    programs: impl Fn(usize) -> (String, String),
) -> (Option<Duration>, usize) {
    let build = |source: &str, tag: &str| {
        let file = work.join(format!("{name}-{tag}.smith"));
        std::fs::write(&file, source).unwrap();
        let status = Command::new(smith)
            .arg("build")
            .arg(&file)
            .current_dir(work)
            .stderr(Stdio::inherit())
            .status()
            .unwrap();
        assert!(status.success(), "smith build {} failed", file.display());
        work.join(file.file_stem().unwrap())
    };
    let net = |reps: usize| {
        let (program, baseline) = programs(reps);
        let (whole, base) = (build(&program, "whole"), build(&baseline, "base"));
        let base = least(&base, None)?;
        let whole = least(&whole, Some(expected))?;
        Some(whole.saturating_sub(base))
    };
    let Some(one) = net(1) else { return (None, 1) };
    if one >= LONG_ENOUGH {
        return (Some(one), 1);
    }
    let reps = (LONG_ENOUGH.as_nanos() / one.as_nanos().max(50_000)).clamp(2, 4096) as usize;
    (net(reps).map(|t| t / reps as u32), reps)
}

/// The least of several runs of an executable, or `None` if one is given up on.
pub(crate) fn least(exe: &Path, expected: Option<&str>) -> Option<Duration> {
    let mut best: Option<Duration> = None;
    for _ in 0..RUNS {
        let start = Instant::now();
        let mut child = Command::new(exe).stdout(Stdio::piped()).spawn().unwrap();
        loop {
            if let Some(status) = child.try_wait().unwrap() {
                let mut said = String::new();
                child
                    .stdout
                    .take()
                    .unwrap()
                    .read_to_string(&mut said)
                    .unwrap();
                if !status.success() || expected.is_some_and(|e| said.trim() != e) {
                    println!(
                        "{} ended with {status} and printed {said:?}, not {expected:?}",
                        exe.display()
                    );
                    return None;
                }
                break;
            }
            if start.elapsed() > GIVE_UP {
                child.kill().ok();
                child.wait().ok();
                return None;
            }
            std::thread::sleep(Duration::from_micros(200));
        }
        let took = start.elapsed();
        best = Some(best.map_or(took, |b| b.min(took)));
        // A run that is slow is not repeated.
        if took > Duration::from_secs(2) {
            break;
        }
    }
    best
}

/// The time of one call of `f`, from as many calls as take a fifth of a second.
pub(crate) fn per_run(mut f: impl FnMut()) -> Duration {
    let start = Instant::now();
    let mut calls = 0u32;
    while start.elapsed() < Duration::from_millis(200) || calls < 3 {
        f();
        calls += 1;
    }
    start.elapsed() / calls
}

fn report(program: &str, who: &str, size: usize, unit: &str, reps: usize, time: Option<Duration>) {
    match time {
        Some(t) => println!(
            "{program} {who} size={size} rounds={reps} total_us={} ns_per_{unit}={:.2}",
            t.as_micros(),
            t.as_nanos() as f64 / size as f64
        ),
        None => println!(
            "{program} {who} size={size} gave up after {} s",
            GIVE_UP.as_secs()
        ),
    }
}
