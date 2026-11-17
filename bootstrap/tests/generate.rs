//! Generated input, from a seed that is the same for one commit, with
//! shrinking.
//!
//! Each property has its own number, and the run has a seed that the
//! number is mixed with. The run's seed is made from the commit the tests
//! are run on, so that the cases differ from one commit to the next and are
//! the same for one commit. `SMITH_SEED=<number>` in the environment sets
//! it, which is how a failure is run again: a run prints its seed first.
//!
//! A generator draws every decision from a `Gen` as a number below some
//! bound, and the `Gen` records each number it gives. A failing input is
//! shrunk by shrinking that record: dropping numbers and making them
//! smaller, then generating again, for as long as the property still fails.
//! A generator therefore makes choice 0 the simplest one, and draws counts
//! before the things it counts.

use std::cell::Cell;
use std::fmt::Debug;
use std::panic::{self, AssertUnwindSafe};
use std::process::Command;
use std::sync::{Once, OnceLock};

/// The source of a generator's choices.
pub struct Gen {
    /// The choices to give, when replaying.
    given: Vec<usize>,
    /// The choices given so far.
    used: Vec<usize>,
    /// The random state, when choices past `given` are drawn at random. A
    /// replay with no random state gives 0 for every choice past `given`.
    random: Option<u64>,
}

static SEED: OnceLock<u64> = OnceLock::new();

/// Fixes the seed of this run, before any generator asks for it.
pub fn set_run_seed(seed: u64) {
    SEED.set(seed).expect("the seed is fixed before it is read");
}

/// The seed of this run: the number in `SMITH_SEED` if there is one, else
/// one made from the commit that is checked out, else 0.
pub fn run_seed() -> u64 {
    *SEED.get_or_init(|| {
        if let Ok(given) = std::env::var("SMITH_SEED") {
            return given.trim().parse().expect("SMITH_SEED is a number");
        }
        let commit = Command::new("git")
            .args(["rev-parse", "HEAD"])
            .current_dir(env!("CARGO_MANIFEST_DIR"))
            .output()
            .ok()
            .filter(|o| o.status.success())
            .map(|o| o.stdout)
            .unwrap_or_default();
        // FNV-1a over the text of the commit's hash.
        commit.iter().fold(0xcbf2_9ce4_8422_2325, |hash, byte| {
            (hash ^ u64::from(*byte)).wrapping_mul(0x0100_0000_01b3)
        })
    })
}

impl Gen {
    fn random(run: u64, seed: u64, case: u64) -> Gen {
        let mut state = seed
            .wrapping_add(run.wrapping_mul(0x94d0_49bb_1331_11eb))
            .wrapping_mul(0x9e37_79b9_7f4a_7c15)
            .wrapping_add(case.wrapping_mul(0xbf58_476d_1ce4_e5b9))
            | 1;
        for _ in 0..4 {
            xorshift(&mut state);
        }
        Gen {
            given: Vec::new(),
            used: Vec::new(),
            random: Some(state),
        }
    }

    /// A source that gives these choices, and then 0 for every choice.
    pub fn replay(choices: &[usize]) -> Gen {
        Gen {
            given: choices.to_vec(),
            used: Vec::new(),
            random: None,
        }
    }

    /// A number from 0 to `n - 1`, where 0 is the simplest choice.
    pub fn below(&mut self, n: usize) -> usize {
        assert!(n > 0, "a choice needs at least one option");
        let at = self.used.len();
        let choice = match (self.given.get(at), &mut self.random) {
            (Some(c), _) => (*c).min(n - 1),
            (None, Some(state)) => (xorshift(state) % n as u64) as usize,
            (None, None) => 0,
        };
        self.used.push(choice);
        choice
    }

    /// One of the items, the first being the simplest.
    pub fn pick<'a, T>(&mut self, items: &'a [T]) -> &'a T {
        &items[self.below(items.len())]
    }

    /// True about once in `n` draws. False is the simpler choice.
    pub fn one_in(&mut self, n: usize) -> bool {
        self.below(n) == n - 1
    }
}

fn xorshift(state: &mut u64) -> u64 {
    *state ^= *state << 13;
    *state ^= *state >> 7;
    *state ^= *state << 17;
    *state
}

/// The input the generator makes for the case of the seed.
pub fn sample<T>(seed: u64, case: u64, generate: impl Fn(&mut Gen) -> T) -> T {
    generate(&mut Gen::random(run_seed(), seed, case))
}

/// A failing input after shrinking: the input, the choices that generate
/// it, and why the property failed for it.
pub struct Failure<T> {
    pub input: T,
    pub choices: Vec<usize>,
    pub message: String,
}

/// Checks a property over `cases` generated inputs. The property fails by
/// panicking. The first failing input is shrunk, and the test fails with
/// the smallest input that still fails, and its message.
pub fn check<T: Debug>(
    seed: u64,
    cases: u64,
    generate: impl Fn(&mut Gen) -> T,
    property: impl Fn(&T),
) {
    if let Some(failure) = find_failure(seed, cases, &generate, &property) {
        panic!(
            "the property fails for {:?}\n{}\n(the smallest of its failing inputs; its choices are {:?}; run again with SMITH_SEED={})",
            failure.input,
            failure.message,
            failure.choices,
            run_seed()
        );
    }
}

/// The first input of `cases` that fails the property, shrunk, if any
/// fails.
pub fn find_failure<T>(
    seed: u64,
    cases: u64,
    generate: &impl Fn(&mut Gen) -> T,
    property: &impl Fn(&T),
) -> Option<Failure<T>> {
    find_failure_in_run(run_seed(), seed, cases, generate, property)
}

/// `find_failure` for the run with the seed `run`, which a test of the
/// shrinker gives so that the failing input it starts from is the same in
/// every run.
pub fn find_failure_in_run<T>(
    run: u64,
    seed: u64,
    cases: u64,
    generate: &impl Fn(&mut Gen) -> T,
    property: &impl Fn(&T),
) -> Option<Failure<T>> {
    (0..cases).find_map(|case| {
        let mut g = Gen::random(run, seed, case);
        let input = generate(&mut g);
        failure(&input, property)?;
        Some(shrink(g.used, generate, property))
    })
}

/// The smallest choices, found by shrinking the given ones, that generate
/// an input that fails the property, and that input. The given choices
/// must generate one that fails.
pub fn shrink<T>(
    choices: Vec<usize>,
    generate: &impl Fn(&mut Gen) -> T,
    property: &impl Fn(&T),
) -> Failure<T> {
    let attempt = |choices: &[usize]| {
        let mut g = Gen::replay(choices);
        let input = generate(&mut g);
        let message = failure(&input, property)?;
        Some(Failure {
            input,
            choices: g.used,
            message,
        })
    };
    let mut best = attempt(&choices).expect("the input to shrink fails");
    // Each step makes the choices smaller in length, then in order, so
    // shrinking ends. The bound only guards against a generator that
    // ignores its choices.
    let mut budget = 20_000;
    let mut improved = true;
    while improved && budget > 0 {
        improved = false;
        let candidates = smaller(&best.choices);
        for candidate in candidates {
            budget -= 1;
            if budget == 0 {
                break;
            }
            if let Some(found) = attempt(&candidate)
                && shortlex_less(&found.choices, &best.choices)
            {
                best = found;
                improved = true;
                break;
            }
        }
    }
    best
}

/// Choices one step smaller than the given ones: with a run of them
/// removed, with a run removed and the count before it made smaller by as
/// many, or with one of them made smaller.
fn smaller(choices: &[usize]) -> Vec<Vec<usize>> {
    let mut out = Vec::new();
    for size in [8, 4, 2, 1] {
        for start in 0..choices.len().saturating_sub(size - 1) {
            let mut shorter = choices[..start].to_vec();
            shorter.extend_from_slice(&choices[start + size..]);
            out.push(shorter.clone());
            if start > 0 && choices[start - 1] >= size {
                shorter[start - 1] -= size;
                out.push(shorter);
            }
        }
    }
    for (i, &c) in choices.iter().enumerate() {
        for lower in [0, c / 2, c.saturating_sub(1)] {
            if lower < c {
                let mut less = choices.to_vec();
                less[i] = lower;
                out.push(less);
            }
        }
    }
    out
}

fn shortlex_less(a: &[usize], b: &[usize]) -> bool {
    (a.len(), a) < (b.len(), b)
}

thread_local! {
    /// Whether a panic on this thread is expected, and is not printed.
    static QUIET: Cell<bool> = const { Cell::new(false) };
}

/// Why the property fails for the input, or `None` if it holds.
fn failure<T>(input: &T, property: &impl Fn(&T)) -> Option<String> {
    static HOOK: Once = Once::new();
    HOOK.call_once(|| {
        let printed = panic::take_hook();
        panic::set_hook(Box::new(move |info| {
            if !QUIET.get() {
                printed(info);
            }
        }));
    });
    QUIET.set(true);
    let result = panic::catch_unwind(AssertUnwindSafe(|| property(input)));
    QUIET.set(false);
    let payload = result.err()?;
    Some(if let Some(s) = payload.downcast_ref::<String>() {
        s.clone()
    } else if let Some(s) = payload.downcast_ref::<&str>() {
        (*s).to_string()
    } else {
        "a panic".to_string()
    })
}
