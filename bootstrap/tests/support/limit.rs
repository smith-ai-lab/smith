//! What a test is judged by when it computes for more than the limit: the
//! least that it computed for in two measures taken in the same run.
//!
//! One measure of the time a thread computes for is not the work of the test.
//! The same test, built the same way, took 385 ms in the run of a pull request
//! and 1040 ms in the run of the merge queue, when the run as a whole and every
//! test beside it took as long as before, and the same test took 250 ms alone.
//! What a machine adds to a test is added once and where the test happens to
//! be, so it is not added twice to the same test. A test that stays over the
//! limit when it is measured again is over it by its work, whatever the
//! machine is, and a test whose work is under it is not failed by a machine
//! that was slow once.
//!
//! A test that computes for less than the limit is measured once, so a run that
//! passes takes no longer for this.

use std::time::Duration;

/// What the test computed for: `first` if that is within `limit`, and
/// otherwise the least of `first` and what `again` measures by running the test
/// once more.
pub fn least(first: Duration, limit: Duration, again: impl FnOnce() -> Duration) -> Duration {
    if first <= limit { first } else { first.min(again()) }
}
