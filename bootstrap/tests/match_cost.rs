//! What a `match` on whole numbers costs: the block it takes does not depend
//! on how many literals there are, and the instructions that run to take it do
//! not grow with them when they lie close together, and grow with their
//! logarithm when they do not.
//!
//! The evaluator counts what runs. Every generator keeps a model of what it
//! writes, in plain Rust.

use smith::target::Target;
use smith::{lower, scalar};

use crate::Test;
use crate::generate::{self, Gen};
use crate::numbers::{INTS, Int, value};
use crate::aarch64;
use crate::targets::{runnable, scalar_runs_as};

pub const ONCE: &[Test] = &[
    (
        "an_arm64_executable_takes_the_block_of_the_literal_a_match_on_many_whole_numbers_equals",
        an_arm64_executable_takes_the_block_of_the_literal_a_match_on_many_whole_numbers_equals,
    ),
    (
        "a_match_on_many_whole_numbers_takes_the_block_of_the_literal_that_equals_it",
        a_match_on_many_whole_numbers_takes_the_block_of_the_literal_that_equals_it,
    ),
    (
        "the_instructions_a_match_runs_do_not_grow_with_literals_that_lie_close_together",
        the_instructions_a_match_runs_do_not_grow_with_literals_that_lie_close_together,
    ),
    (
        "the_instructions_a_match_runs_grow_with_the_logarithm_of_literals_that_lie_far_apart",
        the_instructions_a_match_runs_grow_with_the_logarithm_of_literals_that_lie_far_apart,
    ),
];

pub const MACHINE: &[Test] = &[(
    "every_executable_takes_the_block_of_the_literal_a_match_on_many_whole_numbers_equals",
    every_executable_takes_the_block_of_the_literal_a_match_on_many_whole_numbers_equals,
)];

/// The source of a function `f` of a type that gives the place of the literal
/// it equals among `literals`, or 1000 when it equals none.
fn function(ty: &str, literals: &[i128]) -> String {
    let mut source = format!("fn f(b: {ty}) -> i64 {{\n  match b {{\n");
    for (i, literal) in literals.iter().enumerate() {
        source.push_str(&format!("    {literal} {{ {i} }}\n"));
    }
    source.push_str("    other { 1000 }\n  }\n}\n");
    source
}

/// What `f` gives for the value, by the model.
fn model(literals: &[i128], subject: i128) -> i128 {
    (literals.iter().position(|l| *l == subject)).map_or(1000, |i| i as i128)
}

/// Literals of the type in groups, each with the same distance between its
/// literals, and values to match: each literal and its neighbors, and any.
#[derive(Debug)]
struct Groups {
    ty: Int,
    literals: Vec<i128>,
    subjects: Vec<i128>,
}

fn groups(g: &mut Gen) -> Groups {
    let ty = *g.pick(INTS);
    let mut literals: Vec<i128> = Vec::new();
    for _ in 0..1 + g.below(4) {
        let start = value(g, ty);
        let distance = 1 + g.below(300) as i128;
        let distance = if g.one_in(2) { 1 + distance % 4 } else { distance };
        let direction = if start > ty.max() / 2 { -1 } else { 1 };
        for k in 0..1 + g.below(40) as i128 {
            let n = start + direction * distance * k;
            if (ty.min()..=ty.max()).contains(&n) && !literals.contains(&n) {
                literals.push(n);
            }
        }
    }
    let mut subjects = Vec::new();
    for _ in 0..12 {
        let near = literals[g.below(literals.len())] + g.below(3) as i128 - 1;
        subjects.push(near.clamp(ty.min(), ty.max()));
    }
    for _ in 0..4 {
        subjects.push(value(g, ty));
    }
    Groups {
        ty,
        literals,
        subjects,
    }
}

/// The source of a program that prints what `f` gives for each subject.
fn printing(n: &Groups) -> String {
    let mut source = function(n.ty.name, &n.literals);
    source.push_str("\nfn main() ! console, alloc {\n");
    for (i, s) in n.subjects.iter().enumerate() {
        source.push_str(&format!("  x{i}: {} = {s}\n  print(\"{{f(x{i})}}\")\n", n.ty.name));
    }
    source.push_str("}\n");
    source
}

fn every_executable_takes_the_block_of_the_literal_a_match_on_many_whole_numbers_equals() {
    let targets = runnable();
    generate::check(24, 25, groups, |n: &Groups| {
        let source = printing(n);
        let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
        let said = scalar::run(&program);
        for &target in &targets {
            scalar_runs_as(&program, target, &said);
        }
    });
}

fn a_match_on_many_whole_numbers_takes_the_block_of_the_literal_that_equals_it() {
    generate::check(21, 40, groups, |n: &Groups| {
        let source = printing(n);
        let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
        let out = scalar::run(&program);
        assert_eq!(out.status, 0, "for {source}");
        let expected: Vec<String> = (n.subjects.iter())
            .map(|s| model(&n.literals, *s).to_string())
            .collect();
        let printed = String::from_utf8(out.stdout).expect("text");
        let printed: Vec<&str> = printed.lines().collect();
        assert_eq!(printed, expected, "for {n:?}");
    });
}

/// How many instructions run for a program that calls `f`, written as
/// `function` does, on the subject, and checks what it gives: for the
/// evaluator, which counts what it does, then for each arm64 executable,
/// which the machine in `aarch64` counts the instructions of.
fn instructions(ty: &str, literals: &[i128], subject: i128) -> Vec<u64> {
    let mut source = function(ty, literals);
    let expected = model(literals, subject);
    source.push_str(&format!(
        "\nfn main() ! console {{\n  x: {ty} = {subject}\n  if f(x) == {expected} {{ print(\"yes\") }} else {{ print(\"no\") }}\n}}\n"
    ));
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let (out, ran) = scalar::run_measured(&program);
    assert_eq!(out.stdout, b"yes\n", "for {source}");
    let mut counts = vec![ran];
    for bytes in arm64_executables(&program) {
        let ran = aarch64::run(&bytes);
        assert_eq!(ran.stdout, b"yes\n", "for {source}");
        counts.push(ran.steps);
    }
    counts
}

/// The executables for arm64 of the program that the machine in `aarch64`
/// runs.
fn arm64_executables(program: &scalar::Program) -> Vec<Vec<u8>> {
    (Target::ALL.iter())
        .map(|t| t.emit(program))
        .filter(|bytes| aarch64::is_executable(bytes))
        .collect()
}

/// How many literals the matches have.
const SIZES: [usize; 3] = [4, 32, 256];

/// The values to match for the literals: the first, a middle one, and the
/// last, and one before the first.
fn subjects(literals: &[i128]) -> Vec<i128> {
    let last = literals.len() - 1;
    vec![literals[0], literals[last / 2], literals[last], literals[0] - 1]
}

/// A match of each size, on literals that start at `start` and are `distance`
/// apart, and for each the instructions that run for each of its subjects.
fn counts(ty: &str, start: i128, distance: i128) -> Vec<Vec<Vec<u64>>> {
    SIZES
        .iter()
        .map(|&count| {
            let literals: Vec<i128> = (0..count as i128).map(|k| start + distance * k).collect();
            (subjects(&literals).iter())
                .map(|s| instructions(ty, &literals, *s))
                .collect()
        })
        .collect()
}

/// Checks that the instructions that run for each size of match are at most
/// those for 4 literals, and `more` for each doubling of them, and `loads`
/// more for a machine that must load the larger numbers some matches hold.
fn at_most(counts: &[Vec<Vec<u64>>], more: u64, loads: u64) {
    for (size, ran) in SIZES.iter().zip(counts) {
        let doublings = (*size / 4).ilog2() as u64;
        for (here, least) in ran.iter().zip(&counts[0]) {
            for (machine, (n, m)) in here.iter().zip(least).enumerate() {
                let allowed = m + more * doublings + if machine == 0 { 0 } else { loads };
                assert!(
                    *n <= allowed,
                    "{size} literals run {ran:?} where 4 run {:?}",
                    counts[0]
                );
            }
        }
    }
}

fn the_instructions_a_match_runs_do_not_grow_with_literals_that_lie_close_together() {
    generate::check(
        22,
        2,
        |g| (*g.pick(&["i32", "u32", "i64", "u64"]), g.below(1000) as i128, 1 + g.below(4) as i128),
        |(ty, start, distance): &(&str, i128, i128)| {
            at_most(&counts(ty, *start, *distance), 0, 2);
        },
    );
}

fn the_instructions_a_match_runs_grow_with_the_logarithm_of_literals_that_lie_far_apart() {
    generate::check(
        23,
        2,
        |g| (g.below(1_000_000) as i128, 100_000 + g.below(1_000_000) as i128),
        |(start, distance): &(i128, i128)| {
            // Each doubling of the literals adds a bounded number of
            // instructions, where adding one for each literal would add 32
            // times as many as that for 256.
            at_most(&counts("i64", *start, *distance), 5, 3);
        },
    );
}

fn an_arm64_executable_takes_the_block_of_the_literal_a_match_on_many_whole_numbers_equals() {
    generate::check(25, 15, groups, |n: &Groups| {
        let source = printing(n);
        let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
        let said = scalar::run(&program);
        for bytes in arm64_executables(&program) {
            let ran = aarch64::run(&bytes);
            assert_eq!((ran.stdout, ran.status), (said.stdout.clone(), said.status));
        }
    });
}
