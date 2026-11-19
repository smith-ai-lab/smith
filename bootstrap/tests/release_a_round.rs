//! A round of a loop releases the memory it took from the stack for
//! temporaries, and hands on only what the next round or the code after the
//! loop can read.
//!
//! The programs build lists and texts in the rounds of a loop. Some hand what
//! they build on, each in one of the ways a value can outlive its round: by
//! an assignment to a name that lives past it, by a push to something that
//! does, by a field of one, by a `return`, and by an assignment before a
//! `break`. The others do not. Every program prints what a model of plain
//! numbers says, in the evaluator and in the executable, and the memory the
//! evaluator is asked for does not grow with the rounds of a loop that hands
//! nothing on, and does grow with those of one that does.

use smith::scalar;
use smith::lower;

use crate::Test;
use crate::generate::{self, Gen};
use crate::targets::{runnable, runs_as};

pub const ONCE: &[Test] = &[
    (
        "every_loop_that_builds_lists_and_texts_in_its_rounds_prints_what_the_model_says",
        every_loop_that_builds_lists_and_texts_in_its_rounds_prints_what_the_model_says,
    ),
    (
        "the_memory_asked_for_does_not_grow_with_the_rounds_of_a_loop_that_hands_nothing_on",
        the_memory_asked_for_does_not_grow_with_the_rounds_of_a_loop_that_hands_nothing_on,
    ),
    (
        "the_memory_asked_for_grows_with_the_rounds_of_a_loop_that_hands_what_it_builds_on",
        the_memory_asked_for_grows_with_the_rounds_of_a_loop_that_hands_what_it_builds_on,
    ),
];

pub const MACHINE: &[Test] = &[(
    "every_executable_of_a_loop_that_builds_in_its_rounds_prints_what_the_evaluator_says",
    every_executable_of_a_loop_that_builds_in_its_rounds_prints_what_the_evaluator_says,
)];

/// What a round builds, and whether it hands it on.
#[derive(Clone, Copy, Debug, PartialEq)]
enum Round {
    /// A list that is summed and let go.
    List,
    /// A text whose length is added and let go.
    Text,
    /// Lists held in a list, all of which are let go with it.
    Nested,
    /// A list assigned to a name outside the loop.
    Assigned,
    /// A list pushed to a list outside the loop.
    Pushed,
    /// A list assigned to a field of a value outside the loop.
    Field,
    /// A list given back by a `return` from a function whose loop builds it.
    Returned,
    /// A list assigned to a name outside the loop, and then a `break`.
    Broken,
    /// A text assigned to a name outside the loop.
    KeptText,
}

const ALL: [Round; 9] = [
    Round::List,
    Round::Text,
    Round::Nested,
    Round::Assigned,
    Round::Pushed,
    Round::Field,
    Round::Returned,
    Round::Broken,
    Round::KeptText,
];

impl Round {
    fn hands_on(self) -> bool {
        !matches!(self, Round::List | Round::Text | Round::Nested)
    }

    /// The round, with the rounds `r` and the elements of a list `n`, as a
    /// loop of the program `k`, and what the program adds to its two sums.
    fn code(self, k: usize, n: u64, r: u64) -> (String, String) {
        let half = r / 2;
        let long = "x".repeat(LONG);
        let body = match self {
            Round::List => format!(
                "  var c{k}: u64 = 0\n  loop r{k} in 0..{r} {{\n    ys = build({n}, c{k})\n    sum = wrapping_add(sum, total(ys))\n    c{k} = wrapping_add(c{k}, 1)\n  }}\n"
            ),
            Round::Text => format!(
                "  var c{k}: u64 = 0\n  loop r{k} in 0..{r} {{\n    s = \"{{c{k}}}-{long}\"\n    tsum = wrapping_add(tsum, convert(s.size))\n    c{k} = wrapping_add(c{k}, 1)\n  }}\n"
            ),
            Round::Nested => format!(
                "  var c{k}: u64 = 0\n  loop r{k} in 0..{r} {{\n    var zs: List[List[u64]] = []\n    zs = push(zs, build({n}, c{k}))\n    zs = push(zs, build({n}, wrapping_add(c{k}, 1)))\n    loop z in zs {{ sum = wrapping_add(sum, total(z)) }}\n    c{k} = wrapping_add(c{k}, 1)\n  }}\n"
            ),
            Round::Assigned => format!(
                "  var keep{k}: List[u64] = []\n  var c{k}: u64 = 0\n  loop r{k} in 0..{r} {{\n    keep{k} = build({n}, c{k})\n    c{k} = wrapping_add(c{k}, 1)\n  }}\n  sum = wrapping_add(sum, total(keep{k}))\n"
            ),
            Round::Pushed => format!(
                "  var all{k}: List[List[u64]] = []\n  var c{k}: u64 = 0\n  loop r{k} in 0..{r} {{\n    all{k} = push(all{k}, build({n}, c{k}))\n    c{k} = wrapping_add(c{k}, 1)\n  }}\n  loop z in all{k} {{ sum = wrapping_add(sum, total(z)) }}\n"
            ),
            Round::Field => format!(
                "  var h{k} = Holder(items: [])\n  var c{k}: u64 = 0\n  loop r{k} in 0..{r} {{\n    h{k}.items = build({n}, c{k})\n    c{k} = wrapping_add(c{k}, 1)\n  }}\n  sum = wrapping_add(sum, total(h{k}.items))\n"
            ),
            Round::Returned => format!(
                "  sum = wrapping_add(sum, total(returned({n}, {r}, {half})))\n"
            ),
            Round::Broken => format!(
                "  var found{k}: List[u64] = []\n  var c{k}: u64 = 0\n  loop r{k} in 0..{r} {{\n    ys = build({n}, c{k})\n    if c{k} == {half} {{\n      found{k} = ys\n      break\n    }}\n    c{k} = wrapping_add(c{k}, 1)\n  }}\n  sum = wrapping_add(sum, total(found{k}))\n"
            ),
            Round::KeptText => format!(
                "  var last{k}: str = \"\"\n  var c{k}: u64 = 0\n  loop r{k} in 0..{r} {{\n    last{k} = \"{{c{k}}}-{long}\"\n    c{k} = wrapping_add(c{k}, 1)\n  }}\n  tsum = wrapping_add(tsum, convert(last{k}.size))\n"
            ),
        };
        (body, String::new())
    }

    /// What the loop adds to the sum of numbers and to the sum of lengths.
    fn model(self, n: u64, r: u64) -> (u64, u64) {
        let list = |c: u64| (0..n).map(|i| c + i).sum::<u64>();
        let digits = |c: u64| c.to_string().len() as u64;
        match self {
            Round::List => ((0..r).map(list).sum(), 0),
            Round::Text => (0, (0..r).map(|c| digits(c) + 1 + LONG as u64).sum()),
            Round::Nested => ((0..r).map(|c| list(c) + list(c + 1)).sum(), 0),
            Round::Assigned | Round::Field => (if r == 0 { 0 } else { list(r - 1) }, 0),
            Round::Pushed => ((0..r).map(list).sum(), 0),
            Round::Returned | Round::Broken => (if r / 2 < r { list(r / 2) } else { 0 }, 0),
            Round::KeptText => (0, if r == 0 { 0 } else { digits(r - 1) + 1 + LONG as u64 }),
        }
    }
}

/// How long the texts that rounds build are, after their number and a dash.
const LONG: usize = 480;

const ITEMS: &str = "use std.bits (u64)\nuse std.console (console, print)\nuse std.int (wrapping_add)\nuse std.list (push)\n\nstruct Holder {\n  items: List[u64]\n}\n\nfn build(n: u64, seed: u64) -> List[u64] ! alloc {\n  var ys: List[u64] = []\n  loop i in 0..n { ys = push(ys, wrapping_add(seed, i)) }\n  ys\n}\n\nfn total(xs: List[u64]) -> u64 {\n  var sum: u64 = 0\n  loop v in xs { sum = wrapping_add(sum, v) }\n  sum\n}\n\nfn returned(n: u64, rounds: u64, at: u64) -> List[u64] ! alloc {\n  loop j in 0..rounds {\n    ys = build(n, j)\n    if j == at { return ys }\n  }\n  []\n}\n\n";

/// A program of loops, one for each of `rounds` of the kinds, each with `r`
/// rounds that build lists of `n` elements, and the two numbers it prints.
fn program(rounds: &[(Round, u64, u64)]) -> (String, String) {
    let mut body = String::new();
    let (mut sum, mut tsum) = (0u64, 0u64);
    for (k, (round, n, r)) in rounds.iter().enumerate() {
        body.push_str(&round.code(k, *n, *r).0);
        let (a, b) = round.model(*n, *r);
        sum += a;
        tsum += b;
    }
    let source = format!(
        "{ITEMS}fn main() ! console, alloc {{\n  var sum: u64 = 0\n  var tsum: u64 = 0\n{body}  print(\"{{sum}} {{tsum}}\")\n}}\n"
    );
    (source, format!("{sum} {tsum}\n"))
}

fn rounds(g: &mut Gen) -> Vec<(Round, u64, u64)> {
    (0..1 + g.below(3))
        .map(|_| (*g.pick(&ALL), 1 + g.below(40) as u64, g.below(14) as u64))
        .collect()
}

fn every_loop_that_builds_lists_and_texts_in_its_rounds_prints_what_the_model_says() {
    generate::check(6131, 40, rounds, |rounds| {
        let (source, said) = program(rounds);
        let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{source}"));
        let ran = scalar::run(&program);
        assert_eq!(ran.status, 0, "for {source}");
        assert_eq!(ran.stdout, said.into_bytes(), "for {source}");
    });
}

/// What the system is asked for, in bytes, by a program of one loop.
fn asked(round: Round, rounds: u64) -> usize {
    let (source, said) = program(&[(round, 100, rounds)]);
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{source}"));
    let (ran, requests) = scalar::run_counting(&program, usize::MAX);
    assert_eq!(ran.stdout, said.into_bytes(), "for {source}");
    requests.iter().sum()
}

fn the_memory_asked_for_does_not_grow_with_the_rounds_of_a_loop_that_hands_nothing_on() {
    for round in ALL.into_iter().filter(|r| !r.hands_on()) {
        let (few, many) = (asked(round, 8), asked(round, 160));
        assert_eq!(few, many, "{round:?}: {few} bytes for 8 rounds, {many} for 160");
    }
}

fn the_memory_asked_for_grows_with_the_rounds_of_a_loop_that_hands_what_it_builds_on() {
    for round in ALL.into_iter().filter(|r| r.hands_on()) {
        let (few, many) = (asked(round, 8), asked(round, 160));
        assert!(many > few, "{round:?}: {few} bytes for 8 rounds, {many} for 160");
    }
}

fn every_executable_of_a_loop_that_builds_in_its_rounds_prints_what_the_evaluator_says() {
    let mut all: Vec<(Round, u64, u64)> = ALL.iter().map(|r| (*r, 30, 40)).collect();
    all.push((Round::List, 3, 0));
    for rounds in [all.clone(), vec![all[0]], vec![all[3], all[0], all[4]]] {
        let (source, said) = program(&rounds);
        let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{source}"));
        let ran = scalar::run(&program);
        assert_eq!(ran.stdout, said.into_bytes(), "for {source}");
        for target in runnable() {
            runs_as(source.as_bytes(), target, &ran);
        }
    }
}
