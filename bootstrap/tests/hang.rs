//! When the run takes a test to hang: only when nothing has ended for the whole
//! of the limit, as `clock` says.
//!
//! The run is started again, as a program, with a limit of 400 ms in place of
//! ten seconds, and with the tests of `RUNS` alone, so that these tests do not
//! wait for ten seconds, and the run that checks them is not itself the run
//! being checked.

use crate::Test;
use crate::clock;

pub const ONCE: &[Test] = &[
    (
        "a_test_that_starts_many_programs_that_each_end_in_time_does_not_hang",
        a_test_that_starts_many_programs_that_each_end_in_time_does_not_hang,
    ),
    (
        "a_program_that_never_ends_fails_the_test_that_started_it_saying_so",
        a_program_that_never_ends_fails_the_test_that_started_it_saying_so,
    ),
    (
        "a_test_that_never_ends_without_starting_a_program_fails_by_name_and_the_run_ends",
        a_test_that_never_ends_without_starting_a_program_fails_by_name_and_the_run_ends,
    ),
];

/// The tests the run is started to run, and not part of any other group.
pub const RUNS: &[Test] = &[
    ("demo_many_programs", demo_many_programs),
    ("demo_program_that_never_ends", demo_program_that_never_ends),
    ("demo_test_that_never_ends", demo_test_that_never_ends),
];

/// A run of the tests that is named, and what it printed, on both outputs.
fn run(name: &str) -> (bool, String) {
    let mut command = std::process::Command::new(std::env::current_exe().unwrap());
    command.args(["--group=hang", "--hang=400", name]);
    let output = clock::output_within(&mut command, true).unwrap();
    (
        output.status.success(),
        format!(
            "{}{}",
            String::from_utf8_lossy(&output.stdout),
            String::from_utf8_lossy(&output.stderr)
        ),
    )
}

fn wait(milliseconds: u32) -> std::process::Command {
    let mut command = std::process::Command::new(std::env::current_exe().unwrap());
    command.arg(format!("--wait={milliseconds}"));
    command
}

/// Together they take about four times the limit and the slack it is given.
fn demo_many_programs() {
    for _ in 0..16 {
        clock::output_within(&mut wait(100), false).unwrap();
    }
}

fn demo_program_that_never_ends() {
    clock::output_within(&mut wait(600_000), false).unwrap();
}

fn demo_test_that_never_ends() {
    loop {
        std::thread::sleep(std::time::Duration::from_millis(10));
    }
}

fn a_test_that_starts_many_programs_that_each_end_in_time_does_not_hang() {
    let (passed, printed) = run("demo_many_programs");
    assert!(passed, "{printed}");
    assert!(
        printed.contains("test demo_many_programs ... ok"),
        "{printed}"
    );
    assert!(!printed.contains("hung"), "{printed}");
}

fn a_program_that_never_ends_fails_the_test_that_started_it_saying_so() {
    let (passed, printed) = run("demo_program_that_never_ends");
    assert!(!passed, "{printed}");
    assert!(
        printed.contains("test demo_program_that_never_ends ... FAILED"),
        "{printed}"
    );
    assert!(printed.contains("the program never ends"), "{printed}");
    assert!(!printed.contains("hung"), "{printed}");
}

fn a_test_that_never_ends_without_starting_a_program_fails_by_name_and_the_run_ends() {
    let (passed, printed) = run("demo_test_that_never_ends");
    assert!(!passed, "{printed}");
    assert!(
        printed.contains("hung: demo_test_that_never_ends did not end in"),
        "{printed}"
    );
}
