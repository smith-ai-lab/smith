//! The compiler's tests, and the harness that runs them.
//!
//! The tests run in parallel, in this one process. A run prints each test
//! that failed, and ends with its slowest tests and how long each took.
//! Arguments that do not start with `-` keep only the tests whose names
//! hold one of them.
//!
//! A test that computes for more than a second fails the run, and the run
//! names it, as `LIMIT` says. A test starts no thread, as the time of a
//! thread it starts is not the time of its own. A test that has not ended
//! after `clock::HANG` and a little more on the wall fails the run, and the run names it, so that a
//! program that never ends is a failing test and not a run that never ends.
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
mod arguments;
mod arith;
mod bits;
mod check;
mod choices;
mod cli;
mod clock;
mod contracts;
mod enums;
mod generate;
#[cfg(unix)]
mod install;
mod lambdas;
mod lists;
mod literals;
mod loops;
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
use std::time::{Duration, Instant};

/// A test: its name and what it runs. It fails by panicking.
pub type Test = (&'static str, fn());

/// How many of the slowest tests a run ends with.
const SLOWEST: usize = 70;

/// What a test may compute for, as `clock` measures it.
const LIMIT: Duration = Duration::from_secs(1);

/// What a test is given beyond `clock::HANG`, so that a program it starts that
/// does not end is stopped, and fails the test saying so, before the run gives
/// up on the test.
const SLACK: Duration = Duration::from_secs(2);

/// Whether a test over the limit fails the run. It does on the kind of
/// machine that the `linux` job of `ci` uses, where every test
/// runs, and elsewhere is only printed.
const ENFORCED: bool = cfg!(all(target_os = "linux", target_arch = "x86_64"));

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if args.iter().any(|a| a == "--fingerprint") {
        // The executables are the same for every commit, so that a change
        // that leaves them as they were needs no machine.
        generate::set_run_seed(0);
        for (target, hash) in targets::fingerprint(&targets::machine(), smith::compile) {
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
        arguments::ONCE,
        arith::ONCE,
        bits::ONCE,
        contracts::ONCE,
        lists::ONCE,
        literals::ONCE,
        loops::ONCE,
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
    let workers = workers.min(tests.len());
    // What each worker is running, and since when.
    let running: Vec<Mutex<Option<(&'static str, Instant)>>> =
        (0..workers).map(|_| Mutex::new(None)).collect();
    println!("running {} tests", tests.len());
    println!(
        "seed {} (SMITH_SEED=<it> runs the same cases again)",
        generate::run_seed()
    );
    let mut lost = 0;
    std::thread::scope(|s| {
        for slot in &running {
            s.spawn(|| {
                loop {
                    let i = next.fetch_add(1, Ordering::Relaxed);
                    let Some((name, test)) = tests.get(i) else {
                        break;
                    };
                    *slot.lock().unwrap() = Some((name, Instant::now()));
                    let begin = clock::thread_time();
                    let passed = panic::catch_unwind(AssertUnwindSafe(test)).is_ok();
                    let time = clock::thread_time() - begin;
                    // A test the run gave up on has its result already.
                    if slot.lock().unwrap().take().is_none() {
                        return;
                    }
                    println!("test {name} ... {}", if passed { "ok" } else { "FAILED" });
                    results.lock().unwrap().push((*name, passed, time));
                }
            });
        }
        // A test that does not end cannot be stopped. When one has run for
        // `HANG` it fails, and its worker is let go while the others go on.
        // The run ends when every test has a result, or every worker is held
        // by a test that does not end.
        while results.lock().unwrap().len() < tests.len() && lost < workers {
            std::thread::sleep(Duration::from_millis(20));
            for slot in &running {
                let mut slot = slot.lock().unwrap();
                if let Some((name, since)) = *slot
                    && since.elapsed() > clock::HANG + SLACK
                {
                    *slot = None;
                    lost += 1;
                    println!("test {name} ... FAILED");
                    println!(
                        "hung: {name} did not end in {}, so it was stopped without an end",
                        milliseconds(clock::HANG + SLACK)
                    );
                    results
                        .lock()
                        .unwrap()
                        .push((name, false, clock::HANG + SLACK));
                }
            }
        }
        if lost > 0 {
            // The threads of the tests that did not end cannot be joined,
            // so the report is made here and the process ends with it.
            let ok = report(results.lock().unwrap().clone(), on_machine);
            std::process::exit(if ok { 0 } else { 1 });
        }
    });
    if report(results.into_inner().unwrap(), on_machine) {
        ExitCode::SUCCESS
    } else {
        ExitCode::FAILURE
    }
}

/// Prints how the run went, and gives whether it passed.
fn report(mut results: Vec<(&'static str, bool, Duration)>, on_machine: bool) -> bool {
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
    let slow: Vec<&str> = results
        .iter()
        .filter(|(_, _, time)| *time > LIMIT)
        .map(|(name, _, _)| *name)
        .collect();
    for name in &slow {
        println!(
            "{}: {name} computed for more than {}",
            if ENFORCED { "too slow" } else { "slow" },
            milliseconds(LIMIT)
        );
    }
    failed.is_empty() && !(ENFORCED && !slow.is_empty())
}

fn milliseconds(time: Duration) -> String {
    format!("{:.1} ms", time.as_secs_f64() * 1000.0)
}
