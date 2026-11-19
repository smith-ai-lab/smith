//! A list that grows gets its room after the memory it has when that is the
//! last memory taken from its stack, and is copied only when it is not.
//!
//! The requests the handler of `alloc` is given, and the bytes the runtime's
//! copy moves, are counted by the evaluator, for a list that grows alone and
//! for two that grow at once, in turn.

use smith::scalar;
use smith::lower;

use crate::Test;
use crate::growth;
use crate::targets::{runnable, runs_as};

pub const ONCE: &[Test] = &[
    (
        "a_list_that_grows_alone_asks_once_for_each_doubling_and_copies_only_where_it_cannot_grow_in_place",
        a_list_that_grows_alone_asks_once_for_each_doubling_and_copies_only_where_it_cannot_grow_in_place,
    ),
    (
        "two_lists_that_grow_in_turn_are_each_copied_at_each_doubling_and_read_as_pushed",
        two_lists_that_grow_in_turn_are_each_copied_at_each_doubling_and_read_as_pushed,
    ),
    (
        "a_list_that_grows_in_place_is_read_as_pushed_after_other_lists_ask_for_memory",
        a_list_that_grows_in_place_is_read_as_pushed_after_other_lists_ask_for_memory,
    ),
];

pub const MACHINE: &[Test] = &[(
    "every_executable_reads_the_lists_it_grew_in_place_and_not_in_place_as_pushed",
    every_executable_reads_the_lists_it_grew_in_place_and_not_in_place_as_pushed,
)];

const WIDTH: usize = 8;

/// A program that pushes the numbers below `n` to one list of `u64` and
/// prints the size and the sum of what it holds.
fn alone(n: usize) -> String {
    format!(
        "use std.bits (u64)\nuse std.console (console, print)\nuse std.int (wrapping_add)\nuse std.list (push)\n\nfn main() ! console, alloc {{\n  var xs: List[u64] = []\n  loop i in 0..{n} {{ xs = push(xs, i) }}\n  var sum: u64 = 0\n  loop v in xs {{ sum = wrapping_add(sum, v) }}\n  print(\"{{xs.size}} {{sum}}\")\n}}\n"
    )
}

/// The bytes a list of `n` elements moves when it is copied at each doubling.
fn every_doubling(n: usize) -> usize {
    WIDTH * (n.next_power_of_two() - 1)
}

/// A list pushed to alone is the last memory taken, so it asks for memory once
/// for each doubling, and copies only where its piece had no room for the rest:
/// less than copying at each, and never more.
fn a_list_that_grows_alone_asks_once_for_each_doubling_and_copies_only_where_it_cannot_grow_in_place() {
    let mut saved = 0;
    for n in [1, 2, 3, 4, 5, 100, 1000, 3000, 9000] {
        let source = alone(n);
        let program = lower(source.as_bytes()).unwrap();
        let (outcome, taken) = scalar::run_taking(&program, usize::MAX);
        let (_, _, copied) = scalar::run_copying(&program, usize::MAX);
        let sum = (0..n as u64).sum::<u64>();
        assert_eq!(outcome.stdout, format!("{n} {sum}\n").into_bytes(), "for {n} pushes");
        let rooms = growth::rooms(&taken);
        let doublings = n.next_power_of_two().trailing_zeros() as usize;
        let wanted: Vec<usize> = (0..=doublings).map(|k| WIDTH << k).collect();
        assert_eq!(rooms[..wanted.len()], wanted, "for {n} pushes: {taken:?}");
        // The text that is printed is joined from its parts by the same copy.
        let moved: usize = copied.iter().sum::<usize>() - (outcome.stdout.len() - 1);
        assert_eq!(moved, every_doubling(n) - growth::copied_in_place(&taken), "for {n} pushes");
        saved += growth::copied_in_place(&taken);
    }
    assert!(saved > 0, "no list grew in place");
}

/// Two lists that take turns to grow are each the last memory taken only
/// while the other has room, so a doubling of one that finds the other's memory
/// after its own is a copy, and both lists are read as they were pushed.
fn two_lists_that_grow_in_turn_are_each_copied_at_each_doubling_and_read_as_pushed() {
    for n in [3usize, 40, 700, 2000] {
        let source = two(n);
        let program = lower(source.as_bytes()).unwrap();
        let (outcome, taken) = scalar::run_taking(&program, usize::MAX);
        let (_, _, copied) = scalar::run_copying(&program, usize::MAX);
        let (a, b) = ((0..n as u64).sum::<u64>(), (1..=n as u64).sum::<u64>());
        assert_eq!(outcome.stdout, format!("{n} {n} {a} {b}\n").into_bytes(), "for {n} pushes");
        let doublings = n.next_power_of_two().trailing_zeros() as usize + 1;
        // What is not a power of two of elements is the text that is printed.
        let rooms: Vec<usize> = (growth::rooms(&taken).into_iter()).filter(|r| r % WIDTH == 0 && (r / WIDTH).is_power_of_two()).collect();
        assert_eq!(rooms.len(), 2 * doublings, "for {n} pushes: {taken:?}");
        // The two lists are taken from one stack by turns, so one is always above
        // the other and neither can take the room after its memory, but for a
        // doubling in which the other had not yet asked.
        let moved: usize = copied.iter().sum::<usize>() - (outcome.stdout.len() - 1);
        assert_eq!(moved, 2 * every_doubling(n) - growth::copied_in_place(&taken), "for {n} pushes");
        assert!(growth::copied_in_place(&taken) <= every_doubling(n), "for {n} pushes");
    }
}

/// A list that grows in place is not overwritten by what is taken after it,
/// nor does it overwrite what was taken before: lists built between its
/// pushes are read as they were built.
fn a_list_that_grows_in_place_is_read_as_pushed_after_other_lists_ask_for_memory() {
    let n = 3000;
    let source = "use std.bits (u64)\nuse std.console (console, print)\nuse std.int (wrapping_add)\nuse std.list (push)\n\nfn make(k: u64) -> List[u64] ! alloc {\n  var ys: List[u64] = []\n  loop j in 0..5 { ys = push(ys, wrapping_add(k, j)) }\n  ys\n}\n\nfn main() ! console, alloc {\n  var xs: List[u64] = []\n  var total: u64 = 0\n  var base: u64 = 0\n  loop r in 0..4 {\n    loop i in 0..750 { xs = push(xs, wrapping_add(base, i)) }\n    ys = make(base)\n    base = wrapping_add(base, 750)\n    loop v in ys { total = wrapping_add(total, v) }\n  }\n  var sum: u64 = 0\n  loop v in xs { sum = wrapping_add(sum, v) }\n  print(\"{xs.size} {sum} {total}\")\n}\n";
    let program = lower(source.as_bytes()).unwrap();
    let (outcome, _) = scalar::run_taking(&program, usize::MAX);
    let total: u64 = (0..4u64).map(|r| (0..5).map(|j| 750 * r + j).sum::<u64>()).sum();
    let sum: u64 = (0..n as u64).sum();
    assert_eq!(outcome.stdout, format!("{n} {sum} {total}\n").into_bytes(), "{source}");
}

/// The executable of every target this machine can run prints what the
/// evaluator says, for a list that grows alone, in place where its piece has
/// room, and for two that grow in turn.
fn every_executable_reads_the_lists_it_grew_in_place_and_not_in_place_as_pushed() {
    let mut sources = vec![alone(1), alone(100), alone(9000), alone(40000)];
    sources.push(two(700));
    for source in sources {
        let program = lower(source.as_bytes()).unwrap();
        let said = scalar::run(&program);
        assert_eq!(said.status, 0, "for {source}");
        for target in runnable() {
            runs_as(source.as_bytes(), target, &said);
        }
    }
}

/// A program that pushes to two lists of `u64` in turn, `n` times each, and
/// prints their sizes and sums.
fn two(n: usize) -> String {
    format!(
        "use std.bits (u64)\nuse std.console (console, print)\nuse std.int (wrapping_add)\nuse std.list (push)\n\nfn main() ! console, alloc {{\n  var xs: List[u64] = []\n  var ys: List[u64] = []\n  loop i in 0..{n} {{\n    xs = push(xs, i)\n    ys = push(ys, wrapping_add(i, 1))\n  }}\n  var a: u64 = 0\n  var b: u64 = 0\n  loop v in xs {{ a = wrapping_add(a, v) }}\n  loop v in ys {{ b = wrapping_add(b, v) }}\n  print(\"{{xs.size}} {{ys.size}} {{a}} {{b}}\")\n}}\n"
    )
}
