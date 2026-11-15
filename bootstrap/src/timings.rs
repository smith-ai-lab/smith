//! How long each stage of a build takes, for `--timings`.

use std::time::{Duration, Instant};

/// The stages that have run so far, each with how long it took, and when
/// the build began.
pub struct Timings {
    start: Instant,
    stages: Vec<(&'static str, Duration)>,
}

impl Default for Timings {
    fn default() -> Timings {
        Timings::new()
    }
}

impl Timings {
    pub fn new() -> Timings {
        Timings {
            start: Instant::now(),
            stages: Vec::new(),
        }
    }

    /// Runs one stage and records how long it took, whether or not it
    /// succeeded.
    pub fn stage<T>(&mut self, name: &'static str, run: impl FnOnce() -> T) -> T {
        let begin = Instant::now();
        let result = run();
        self.stages.push((name, begin.elapsed()));
        result
    }

    /// One line for each stage that ran, and then one for the total.
    pub fn report(&self) -> String {
        let total = self.start.elapsed();
        let mut lines: Vec<String> = self
            .stages
            .iter()
            .map(|(name, time)| line(name, *time))
            .collect();
        lines.push(line("total", total));
        lines.join("\n")
    }
}

fn line(name: &str, time: Duration) -> String {
    format!("{name:<9}{:.2} ms", time.as_secs_f64() * 1000.0)
}
