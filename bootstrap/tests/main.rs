//! The compiler's tests, and the harness that runs them.
//!
//! The tests run in parallel, in this one process. A run prints each test
//! that failed, and ends with its slowest tests and how long each took.
//! Arguments that do not start with `-` keep only the tests whose names
//! hold one of them.
//!
//! A test that computes for more than a second fails the run, and the run
//! names it, as `LIMIT` says.
//!
//! `--fingerprint` runs nothing. It prints, for each target, a hash of
//! everything the group that depends on the machine would run for it, as
//! `.github/needs-machines.sh` reads it.
//!
//! The tests are in two groups. `--group=machine` runs only the tests whose
//! result depends on the kind of machine they run on, and `--group=once`
//! runs only the others. A run that includes the first group ends by
//! naming the targets this machine could not run.

mod alloc;
mod check;
mod choices;
mod cli;
mod clock;
mod enums;
mod generate;
#[cfg(unix)]
mod install;
mod lambdas;
mod machines;
mod numbers;
mod programs;
mod props;
mod release;
mod reprs;
mod scalar;
mod sha256;
mod targets;
mod text;
mod traits;
mod tries;
mod tuples;

use std::panic::{self, AssertUnwindSafe};
use std::process::ExitCode;
use std::sync::Mutex;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::time::Duration;

/// A test: its name and what it runs. It fails by panicking.
pub type Test = (&'static str, fn());

/// How many of the slowest tests a run ends with.
const SLOWEST: usize = 5;

/// What a test may compute for, as `clock` measures it.
const LIMIT: Duration = Duration::from_secs(1);

/// The tests the limit applies to, a file at a time. A file joins when every
/// test in it is under the limit, and never leaves. This list is a stopgap:
/// the issue that adds the last file deletes it, and the limit then applies
/// to every test with nothing here to keep by hand. The files that are not
/// here are `machines.rs`, whose tests share a fingerprint that is made in
/// `targets.rs`, and `targets.rs`, which is being rewritten.
fn limited() -> Vec<Test> {
    let tests = [
        props::TESTS,
        cli::ONCE,
        cli::MACHINE,
        check::ONCE,
        release::ONCE,
        scalar::ONCE,
    ]
    .concat();
    // The installer is for macOS and Linux, and its tests run where a shell does.
    #[cfg(unix)]
    let tests = [tests, install::ONCE.to_vec()].concat();
    tests
}

/// Whether a test over the limit fails the run. It does on the kind of
/// machine that the `linux` job of `ci` uses, where every limited test
/// runs, and elsewhere is only printed.
const ENFORCED: bool = cfg!(all(target_os = "linux", target_arch = "x86_64"));

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.iter().any(|a| a == "--fingerprint") {
        for (target, hash) in targets::fingerprint(smith::compile) {
            println!("{} {hash}", target.name());
        }
        return ExitCode::SUCCESS;
    }
    let filters: Vec<&String> = args.iter().filter(|a| !a.starts_with('-')).collect();
    let once = [
        props::TESTS,
        cli::ONCE,
        check::ONCE,
        release::ONCE,
        machines::ONCE,
        targets::ONCE,
        scalar::ONCE,
        alloc::ONCE,
    ]
    .concat();
    // The installer is for macOS and Linux, and its tests run where a shell does.
    #[cfg(unix)]
    let once = [once, install::ONCE.to_vec()].concat();
    let machine = targets::machine();
    let groups: Vec<&[Test]> = match args.iter().find_map(|a| a.strip_prefix("--group=")) {
        None => vec![&once, &machine],
        Some("once") => vec![&once],
        Some("machine") => vec![&machine],
        Some(other) => {
            eprintln!("unknown group `{other}`; the groups are once and machine");
            return ExitCode::FAILURE;
        }
    };
    let tests: Vec<Test> = groups
        .concat()
        .into_iter()
        .filter(|(name, _)| filters.is_empty() || filters.iter().any(|f| name.contains(f.as_str())))
        .collect();
    let on_machine = tests
        .iter()
        .any(|(name, _)| machine.iter().any(|(m, _)| m == name));
    let next = AtomicUsize::new(0);
    let results = Mutex::new(Vec::new());
    let workers = std::thread::available_parallelism().map_or(4, |n| n.get());
    println!("running {} tests", tests.len());
    std::thread::scope(|s| {
        for _ in 0..workers.min(tests.len()) {
            s.spawn(|| {
                loop {
                    let i = next.fetch_add(1, Ordering::Relaxed);
                    let Some((name, test)) = tests.get(i) else {
                        break;
                    };
                    let begin = clock::thread_time();
                    let passed = panic::catch_unwind(AssertUnwindSafe(test)).is_ok();
                    let time = clock::thread_time() - begin;
                    println!("test {name} ... {}", if passed { "ok" } else { "FAILED" });
                    results.lock().unwrap().push((*name, passed, time));
                }
            });
        }
    });
    let mut results = results.into_inner().unwrap();
    let failed: Vec<&str> = results
        .iter()
        .filter(|(_, passed, _)| !passed)
        .map(|(name, _, _)| *name)
        .collect();
    println!();
    for name in &failed {
        println!("failed: {name}");
    }
    println!(
        "{} passed, {} failed",
        results.len() - failed.len(),
        failed.len()
    );
    if on_machine {
        println!();
        for line in targets::not_run() {
            println!("{line}");
        }
    }
    results.sort_by_key(|(_, _, time)| std::cmp::Reverse(*time));
    println!("\nslowest tests, by the time they spent computing:");
    for (name, _, time) in results.iter().take(SLOWEST) {
        println!("{:>10}  {name}", milliseconds(*time));
    }
    let limited = limited();
    let slow: Vec<&str> = results
        .iter()
        .filter(|(name, _, time)| *time > LIMIT && limited.iter().any(|(l, _)| l == name))
        .map(|(name, _, _)| *name)
        .collect();
    for name in &slow {
        println!(
            "{}: {name} computed for more than {}",
            if ENFORCED { "too slow" } else { "slow" },
            milliseconds(LIMIT)
        );
    }
    if failed.is_empty() && !(ENFORCED && !slow.is_empty()) {
        ExitCode::SUCCESS
    } else {
        ExitCode::FAILURE
    }
}

fn milliseconds(time: Duration) -> String {
    format!("{:.1} ms", time.as_secs_f64() * 1000.0)
}
