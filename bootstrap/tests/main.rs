//! The compiler's tests, and the harness that runs them.
//!
//! The tests run in parallel, in this one process. A run prints each test
//! that failed, and ends with its slowest tests and how long each took.
//! Arguments that do not start with `-` keep only the tests whose names
//! hold one of them.

mod cli;
mod generate;
mod programs;
mod props;

use std::panic::{self, AssertUnwindSafe};
use std::process::ExitCode;
use std::sync::Mutex;
use std::sync::atomic::{AtomicUsize, Ordering};
use std::time::{Duration, Instant};

/// A test: its name and what it runs. It fails by panicking.
pub type Test = (&'static str, fn());

/// How many of the slowest tests a run ends with.
const SLOWEST: usize = 5;

fn main() -> ExitCode {
    let filters: Vec<String> = std::env::args()
        .skip(1)
        .filter(|a| !a.starts_with('-'))
        .collect();
    let tests: Vec<Test> = [props::TESTS, cli::TESTS]
        .concat()
        .into_iter()
        .filter(|(name, _)| filters.is_empty() || filters.iter().any(|f| name.contains(f.as_str())))
        .collect();
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
                    let begin = Instant::now();
                    let passed = panic::catch_unwind(AssertUnwindSafe(test)).is_ok();
                    let time = begin.elapsed();
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
    results.sort_by_key(|(_, _, time)| std::cmp::Reverse(*time));
    println!("\nslowest tests:");
    for (name, _, time) in results.iter().take(SLOWEST) {
        println!("{:>10}  {name}", milliseconds(*time));
    }
    if failed.is_empty() {
        ExitCode::SUCCESS
    } else {
        ExitCode::FAILURE
    }
}

fn milliseconds(time: Duration) -> String {
    format!("{:.1} ms", time.as_secs_f64() * 1000.0)
}
