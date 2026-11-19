//! The judgment of how long a test computed for, which `support/limit.rs` makes
//! from two measures, on both sides of it: a test of a known work that a slowed
//! run measures as far over the limit passes, and a test of ten times that work
//! fails on a run that is not slowed.

use std::cell::Cell;
use std::hint::black_box;
use std::time::Duration;

use crate::Test;
use crate::clock::thread_time;
use crate::limit::least;

pub const ONCE: &[Test] = &[
    (
        "a_test_of_a_known_work_passes_a_run_that_slowed_it_once",
        a_test_of_a_known_work_passes_a_run_that_slowed_it_once,
    ),
    (
        "a_test_of_ten_times_the_work_fails_a_run_that_did_not_slow_it",
        a_test_of_ten_times_the_work_fails_a_run_that_did_not_slow_it,
    ),
    (
        "a_test_within_the_limit_is_measured_once",
        a_test_within_the_limit_is_measured_once,
    ),
];

/// Work that is the same on every machine: the number of rounds, and nothing
/// else, is what a test of this kind does.
const ROUNDS: u64 = 3_000_000;

/// What running that many rounds computed for.
fn spent(rounds: u64) -> Duration {
    let begin = thread_time();
    let mut x = 1u64;
    for i in 0..rounds {
        x = black_box(x.wrapping_mul(6364136223846793005).wrapping_add(i));
    }
    black_box(x);
    thread_time() - begin
}

/// The limit of these tests: three times what the known work computed for.
fn limit() -> Duration {
    spent(ROUNDS) * 3
}

/// A run that slowed the test once: the first measure has the work and eleven
/// times the work, as a machine that gave the test a tenth of its speed would,
/// and the second is the work.
fn a_test_of_a_known_work_passes_a_run_that_slowed_it_once() {
    let limit = limit();
    let first = spent(ROUNDS) + spent(10 * ROUNDS);
    assert!(first > limit, "the first measure is over the limit");
    let judged = least(first, limit, || spent(ROUNDS));
    assert!(
        judged <= limit,
        "{judged:?} is over {limit:?} for a work that is under it"
    );
}

/// A run that is not slowed: both measures are of ten times the work.
fn a_test_of_ten_times_the_work_fails_a_run_that_did_not_slow_it() {
    let limit = limit();
    let judged = least(spent(10 * ROUNDS), limit, || spent(10 * ROUNDS));
    assert!(
        judged > limit,
        "{judged:?} is under {limit:?} for a work that is over it"
    );
}

fn a_test_within_the_limit_is_measured_once() {
    let measured = Cell::new(0);
    let first = spent(ROUNDS);
    let judged = least(first, first + Duration::from_millis(1), || {
        measured.set(measured.get() + 1);
        Duration::ZERO
    });
    assert_eq!(judged, first);
    assert_eq!(measured.get(), 0, "a test within the limit is run again");
}
