//! What a function leaves behind: its temporaries are released when it
//! returns, and its result is built in its caller's memory and never copied.
//! A program that runs long asks the system for memory a few times and no
//! more, whatever it builds, passes, returns, and holds in a struct, a tuple,
//! or an enum, and it prints what it printed before memory was given back.

use smith::lower;
use smith::scalar::{self, Op, Program, handler};

use crate::Test;
use crate::generate::{self, Gen};
use crate::targets::{runnable, runs_as};

pub const ONCE: &[Test] = &[
    (
        "a_function_called_again_and_again_asks_the_system_for_nothing_after_the_first_calls",
        a_function_called_again_and_again_asks_the_system_for_nothing_after_the_first_calls,
    ),
    (
        "temporaries_larger_than_a_piece_are_found_again_by_the_next_call",
        temporaries_larger_than_a_piece_are_found_again_by_the_next_call,
    ),
    (
        "a_list_returned_through_calls_is_written_once",
        a_list_returned_through_calls_is_written_once,
    ),
    (
        "what_a_function_builds_prints_what_the_generator_says_when_every_value_is_read_after_the_calls_that_could_release_it",
        what_a_function_builds_prints_what_the_generator_says_when_every_value_is_read_after_the_calls_that_could_release_it,
    ),
    (
        "a_function_takes_the_stacks_it_uses_and_no_others",
        a_function_takes_the_stacks_it_uses_and_no_others,
    ),
    (
        "what_is_read_out_of_a_temporary_and_returned_is_still_there_after_a_call_that_would_overwrite_what_was_released",
        what_is_read_out_of_a_temporary_and_returned_is_still_there_after_a_call_that_would_overwrite_what_was_released,
    ),
];

pub const MACHINE: &[Test] = &[
    (
        "every_executable_prints_what_the_evaluator_says_when_values_are_built_passed_and_returned",
        every_executable_prints_what_the_evaluator_says_when_values_are_built_passed_and_returned,
    ),
    (
        "every_executable_reads_what_was_returned_out_of_a_temporary_as_the_evaluator_does",
        every_executable_reads_what_was_returned_out_of_a_temporary_as_the_evaluator_does,
    ),
];

/// The source of `squares` and `sum_of_squares`, as `arenas.md` writes them.
const SQUARES: &str = "fn squares(n: u64) -> List[u64] ! alloc {\n  var acc: List[u64] = []\n  loop i in 0..n { acc = push(acc, wrapping_mul(i, i)) }\n  acc\n}\n\nfn sum_of_squares(n: u64) -> u64 ! alloc {\n  xs = squares(n)\n  var total = 0\n  loop i in 0..xs.size { total = wrapping_add(total, xs[i]) }\n  total\n}\n\n";

/// How many times the program asks the system for memory, and what it
/// printed.
fn asks(source: &str) -> (usize, Vec<u8>) {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let (outcome, requests) = scalar::run_counting(&program, usize::MAX);
    assert_eq!(outcome.status, 0, "for {source}");
    (requests.len(), outcome.stdout)
}

/// A program that calls `sum_of_squares` of `n` as many times as it is told to.
fn calling(calls: u64, n: u64) -> String {
    format!(
        "{SQUARES}fn main() ! console, alloc {{\n  var sum = 0\n  loop i in 0..{calls} {{ sum = wrapping_add(sum, sum_of_squares({n})) }}\n  print(\"{{sum}}\")\n}}\n"
    )
}

/// The number of times the system is asked does not grow with the number of
/// calls, and the calls add up what they should.
fn a_function_called_again_and_again_asks_the_system_for_nothing_after_the_first_calls() {
    for n in [1, 7, 40] {
        let one_square: u64 = (0..n).map(|i| i * i).sum();
        let mut asked = Vec::new();
        for calls in [3, 30, 90] {
            let (requests, stdout) = asks(&calling(calls, n));
            assert_eq!(stdout, format!("{}\n", calls * one_square).into_bytes());
            asked.push(requests);
        }
        assert_eq!(asked[0], asked[1], "for {n} elements: {asked:?}");
        assert_eq!(asked[1], asked[2], "for {n} elements: {asked:?}");
    }
}

/// What a function holds while it runs may take several pieces, and the pieces
/// the first call asked for are the ones the next calls use, through the word
/// each piece ends with.
fn temporaries_larger_than_a_piece_are_found_again_by_the_next_call() {
    let (first, _) = asks(&calling(1, 4000));
    let (many, stdout) = asks(&calling(6, 4000));
    assert_eq!(many, first);
    let one_square: u64 = (0..4000u64).map(|i| i * i).sum();
    assert_eq!(stdout, format!("{}\n", 6 * one_square).into_bytes());
    assert!(many > 2, "{many} requests do not need a second piece");
}

/// A list a function returns, through the functions that return it, is
/// written where the first caller keeps it and nowhere else: the bytes taken
/// for lists are the bytes of that list, once.
fn a_list_returned_through_calls_is_written_once() {
    generate::check(
        310,
        40,
        |g| (1 + g.below(12), 1 + g.below(6)),
        |&(items, depth)| {
            let list: Vec<String> = (0..items).map(|k| (k * 3 + 1).to_string()).collect();
            let mut source = format!(
                "fn r0() -> List[u64] ! alloc {{ [{}] }}\n\n",
                list.join(", ")
            );
            for k in 1..=depth {
                source += &format!("fn r{k}() -> List[u64] ! alloc {{ r{}() }}\n\n", k - 1);
            }
            source += &format!(
                "fn main() ! alloc {{\n  ys = r{depth}()\n  if ys.size == {items} {{ }}\n}}\n"
            );
            let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
            let (outcome, taken) = scalar::run_taking(&program, usize::MAX);
            assert_eq!(outcome.status, 0);
            assert_eq!(taken, vec![8 * items], "for {source}");
        },
    );
}

/// What a generated program is built from and prints.
struct Case {
    source: String,
    stdout: Vec<u8>,
}

impl std::fmt::Debug for Case {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.source)
    }
}

/// What every level returns, as the generator keeps it: the elements of a
/// list and a text.
#[derive(Clone)]
struct Held {
    xs: Vec<u64>,
    label: String,
}

/// What a level of the generated program does.
#[derive(Clone, Debug)]
enum Level {
    /// Builds a list and does not return it, and returns what the level
    /// below returns for the number `a` made larger by its size.
    Pass { temp: u64 },
    /// Calls the level below twice, builds a list and a text it does not
    /// return, and returns what it holds of those and of the two results.
    Combine {
        temp: u64,
        /// Whether the result starts from what the second call gave.
        from_second: bool,
        add_temp: bool,
        add_other: bool,
        /// Whether the result is an enum's second case, which holds a tuple, and
        /// not its first, which holds a struct.
        tuple: bool,
    },
}

/// The items every program has: what a level returns, and the functions that
/// take it apart or build a list.
const ITEMS: &str = "struct Pack {\n  xs: List[u64]\n  label: str\n}\n\nenum Held {\n  one: Pack\n  two: (List[u64], str)\n}\n\nfn first(t: (List[u64], str)) -> List[u64] {\n  match t {\n    (xs, s) { xs }\n  }\n}\n\nfn second(t: (List[u64], str)) -> str {\n  match t {\n    (xs, s) { s }\n  }\n}\n\nfn xs_of(h: Held) -> List[u64] {\n  match h {\n    .one(p) { p.xs }\n    .two(t) { first(t) }\n  }\n}\n\nfn label_of(h: Held) -> str {\n  match h {\n    .one(p) { p.label }\n    .two(t) { second(t) }\n  }\n}\n\nfn total(xs: List[u64]) -> u64 {\n  var s = 0\n  loop i in 0..xs.size { s = wrapping_add(s, xs[i]) }\n  s\n}\n\nfn build(n: u64, seed: u64) -> List[u64] ! alloc {\n  var acc: List[u64] = []\n  loop i in 0..n { acc = push(acc, wrapping_add(wrapping_mul(i, seed), 1)) }\n  acc\n}\n\n";

fn built(n: u64, seed: u64) -> Vec<u64> {
    (0..n)
        .map(|i| i.wrapping_mul(seed).wrapping_add(1))
        .collect()
}

/// What the level `k` returns for `a`.
fn returns(levels: &[Level], base: (u64, bool), k: usize, a: u64) -> Held {
    if k == 0 {
        return Held {
            xs: built(base.0, a),
            label: format!("b{a}"),
        };
    }
    match &levels[k - 1] {
        Level::Pass { temp } => returns(levels, base, k - 1, a.wrapping_add(*temp)),
        Level::Combine {
            temp,
            from_second,
            add_temp,
            add_other,
            ..
        } => {
            let p = returns(levels, base, k - 1, a);
            let q = returns(levels, base, k - 1, a.wrapping_add(1));
            let (start, other) = if *from_second { (q, p) } else { (p, q) };
            let mut xs = start.xs;
            if *add_temp {
                xs.extend(built(*temp, a));
            }
            if *add_other {
                xs.extend(other.xs);
            }
            Held {
                xs,
                label: format!("{}/n{a}", start.label),
            }
        }
    }
}

/// The source of the function of level `k`, which is the function `f{k}`.
fn level(k: usize, what: &Level) -> String {
    let below = k - 1;
    match what {
        Level::Pass { temp } => format!(
            "fn f{k}(a: u64) -> Held ! alloc {{\n  t = build({temp}, a)\n  r = f{below}(wrapping_add(a, convert(t.size)))\n  r\n}}\n\n"
        ),
        Level::Combine {
            temp,
            from_second,
            add_temp,
            add_other,
            tuple,
        } => {
            let (start, other) = if *from_second { ("q", "p") } else { ("p", "q") };
            let mut body = format!(
                "  p = f{below}(a)\n  q = f{below}(wrapping_add(a, 1))\n  t = build({temp}, a)\n  note = \"n{{a}}\"\n  var acc: List[u64] = xs_of({start})\n"
            );
            if *add_temp {
                body += "  loop i in 0..t.size { acc = push(acc, t[i]) }\n";
            }
            if *add_other {
                body += &format!(
                    "  more = xs_of({other})\n  loop i in 0..more.size {{ acc = push(acc, more[i]) }}\n"
                );
            }
            body += &format!("  from = label_of({start})\n  label = \"{{from}}/{{note}}\"\n");
            body += if *tuple {
                "  .two((acc, label))\n"
            } else {
                "  .one(Pack(xs: acc, label: label))\n"
            };
            format!("fn f{k}(a: u64) -> Held ! alloc {{\n{body}}}\n\n")
        }
    }
}

/// A program in which functions build lists and texts, pass them, return them
/// in a struct, a tuple, and an enum, and call each other to some depth, and
/// whose `main` reads every value it was given after it has called all the rest.
fn program(g: &mut Gen) -> Case {
    let depth = 1 + g.below(4);
    let base = (g.below(5) as u64, g.one_in(2));
    let levels: Vec<Level> = (0..depth)
        .map(|_| {
            if g.one_in(4) {
                Level::Pass {
                    temp: g.below(5) as u64,
                }
            } else {
                Level::Combine {
                    temp: g.below(5) as u64,
                    from_second: g.one_in(2),
                    add_temp: g.one_in(2),
                    add_other: g.one_in(2),
                    tuple: g.one_in(2),
                }
            }
        })
        .collect();
    let mut source = ITEMS.to_string();
    source += &format!(
        "fn f0(a: u64) -> Held ! alloc {{\n  xs = build({}, a)\n  label = \"b{{a}}\"\n  {}\n}}\n\n",
        base.0,
        if base.1 {
            ".two((xs, label))"
        } else {
            ".one(Pack(xs: xs, label: label))"
        }
    );
    for (k, what) in levels.iter().enumerate() {
        source += &level(k + 1, what);
    }
    let calls: Vec<u64> = (0..1 + g.below(3)).map(|_| g.below(9) as u64).collect();
    source += "fn main() ! console, alloc {\n";
    for (j, seed) in calls.iter().enumerate() {
        source += &format!("  r{j} = f{depth}({seed})\n");
    }
    let mut stdout = Vec::new();
    for (j, seed) in calls.iter().enumerate() {
        source += &format!(
            "  l{j} = label_of(r{j})\n  s{j} = total(xs_of(r{j}))\n  n{j} = xs_of(r{j}).size\n  print(\"{{l{j}}} {{s{j}}} {{n{j}}}\")\n"
        );
        let held = returns(&levels, base, depth, *seed);
        let sum = held.xs.iter().fold(0u64, |s, x| s.wrapping_add(*x));
        stdout.extend(format!("{} {sum} {}\n", held.label, held.xs.len()).bytes());
    }
    source += "}\n";
    Case { source, stdout }
}

fn what_a_function_builds_prints_what_the_generator_says_when_every_value_is_read_after_the_calls_that_could_release_it()
 {
    generate::check(311, 30, program, |case| {
        let program = lower(case.source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
        assert_eq!(scalar::check(&program), Ok(()));
        assert_eq!(scalar::run(&program).stdout, case.stdout);
        assert_eq!(scalar::run(&scalar::passes(&program)).stdout, case.stdout);
    });
}

fn every_executable_prints_what_the_evaluator_says_when_values_are_built_passed_and_returned() {
    let targets = runnable();
    generate::check(312, 25, program, |case| {
        let program = lower(case.source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
        let said = scalar::run(&scalar::passes(&program));
        assert_eq!(said.stdout, case.stdout);
        for &target in &targets {
            runs_as(case.source.as_bytes(), target, &said);
        }
    });
}

/// The functions that the function at `from` calls, other than the handler's, in
/// the order they are called.
fn called_by(program: &Program, from: usize) -> Vec<usize> {
    let take = handler::take();
    let at = (program.functions.iter().position(|f| *f == take)).expect("a function that takes");
    let ready = handler::ready(at);
    let mut seen = Vec::new();
    for i in (program.functions[from].blocks.iter()).flat_map(|b| &b.instructions) {
        if let Op::Call { function, .. } = i.op
            && !seen.contains(&function)
            && program.functions[function] != take
            && program.functions[function] != ready
        {
            seen.push(function);
        }
    }
    seen
}

/// A function that asks for nothing takes no stack, and one whose every
/// request is its result takes the stack for results and no other, and gives
/// it back, as a function that has a temporary takes both.
fn a_function_takes_the_stacks_it_uses_and_no_others() {
    let source = "fn plain(n: u64) -> u64 { wrapping_add(n, 1) }\n\nfn result() -> List[u64] ! alloc { [1, 2, 3] }\n\nfn temporary() -> Nat ! alloc {\n  xs: List[u64] = [1, 2, 3]\n  xs.size\n}\n\nfn main() ! alloc {\n  a = plain(1)\n  b = result()\n  c = temporary()\n}\n";
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
    // What the program starts with calls `main`.
    let main = called_by(&program, program.main)[0];
    let called: Vec<(usize, usize)> = called_by(&program, main)
        .into_iter()
        .map(|f| {
            let f = &program.functions[f];
            (f.blocks[0].params.len(), f.results.len())
        })
        .collect();
    // `plain` takes its number and gives one, `result` takes one stack and
    // gives it back before the three words of its list, and `temporary` takes
    // the stack for temporaries, and its caller has no use for it after.
    assert_eq!(called, vec![(1, 1), (2, 5), (2, 1)]);
}

/// What a function returns that it read out of a temporary, or chose between
/// two of.
#[derive(Clone, Copy, Debug)]
enum Read {
    /// An element of a list of lists, or a list the function builds when
    /// there is none at that place.
    Element { n: u64, at: u64 },
    /// A field of an element of a list of structs, or a list the function
    /// builds when there is none at that place.
    Field { n: u64, at: u64 },
    /// One of two lists the function builds, chosen by a condition.
    Chosen { n: u64 },
    /// A list of two lists the function builds.
    Pair { n: u64 },
    /// The text of bytes the function builds, in a `Result`.
    Text { letters: u64 },
}

/// The items of the programs that read what was returned.
const READS: &str = "struct Cell {\n  xs: List[u64]\n  k: u64\n}\n\nfn squares(n: u64) -> List[u64] ! alloc {\n  var acc: List[u64] = []\n  loop i in 0..n { acc = push(acc, wrapping_mul(i, i)) }\n  acc\n}\n\nfn total(xs: List[u64]) -> u64 {\n  var t = 0\n  loop i in 0..xs.size { t = wrapping_add(t, xs[i]) }\n  t\n}\n\nfn sum_sq(n: u64) -> u64 ! alloc { total(squares(n)) }\n\n";

fn squares(n: u64) -> Vec<u64> {
    (0..n).map(|i| i.wrapping_mul(i)).collect()
}

/// What a read is in a program: the source of its function `read{k}`, the call
/// in `main` that makes it, the lines of `main` that print what it returned, and
/// what they print.
struct Part {
    item: String,
    call: String,
    show: String,
    line: String,
}

fn both(xs: &[u64]) -> String {
    let sum = xs.iter().fold(0u64, |s, x| s.wrapping_add(*x));
    format!("{sum} {}", xs.len())
}

fn read(k: usize, what: Read) -> Part {
    let list = |item: String, call: String, line: String| Part {
        item,
        call,
        show: format!("  print(\"{{total(r{k})}} {{r{k}.size}}\")\n"),
        line,
    };
    match what {
        Read::Element { n, at } => list(
            format!("fn read{k}(n: u64, at: Nat) -> List[u64] ! alloc {{\n  var all: List[List[u64]] = []\n  loop i in 0..n {{ all = push(all, squares(wrapping_add(i, 1))) }}\n  junk = squares(50)\n  if at < all.size {{ all[at] }} else {{ squares(1) }}\n}}\n\n"),
            format!("  r{k} = read{k}({n}, {at})\n"),
            both(&if at < n { squares(at + 1) } else { squares(1) }),
        ),
        Read::Field { n, at } => list(
            format!("fn read{k}(n: u64, at: Nat) -> List[u64] ! alloc {{\n  var cells: List[Cell] = []\n  loop i in 0..n {{ cells = push(cells, Cell(xs: squares(wrapping_add(i, 1)), k: i)) }}\n  junk = squares(50)\n  if at < cells.size {{ cells[at].xs }} else {{ squares(1) }}\n}}\n\n"),
            format!("  r{k} = read{k}({n}, {at})\n"),
            both(&if at < n { squares(at + 1) } else { squares(1) }),
        ),
        Read::Chosen { n } => list(
            format!("fn read{k}(n: u64) -> List[u64] ! alloc {{\n  a = squares(n)\n  b = squares(wrapping_add(n, 2))\n  junk = squares(40)\n  if n < 4 {{ a }} else {{ b }}\n}}\n\n"),
            format!("  r{k} = read{k}({n})\n"),
            both(&if n < 4 { squares(n) } else { squares(n + 2) }),
        ),
        Read::Pair { n } => Part {
            item: format!("fn read{k}(n: u64) -> List[List[u64]] ! alloc {{\n  a = squares(n)\n  b = squares(wrapping_add(n, 1))\n  junk = squares(40)\n  [a, b]\n}}\n\n"),
            call: format!("  r{k} = read{k}({n})\n"),
            show: format!("  if 1 < r{k}.size {{\n    print(\"{{total(r{k}[0])}} {{r{k}[0].size}} {{total(r{k}[1])}} {{r{k}[1].size}}\")\n  }}\n"),
            line: format!("{} {}", both(&squares(n)), both(&squares(n + 1))),
        },
        Read::Text { letters } => {
            let bytes: Vec<String> = (0..letters).map(|i| (97 + i).to_string()).collect();
            let text: String = (0..letters).map(|i| char::from(97 + i as u8)).collect();
            Part {
                item: format!("fn read{k}() -> Result[str, Utf8Error] ! alloc {{\n  b: List[u8] = [{}]\n  junk = squares(40)\n  utf8(b)\n}}\n\n", bytes.join(", ")),
                call: format!("  r{k} = read{k}()\n"),
                show: format!("  match r{k} {{\n    .ok(t) {{ print(\"{{t}}\") }}\n    .err(e) {{ print(\"err\") }}\n  }}\n"),
                line: text,
            }
        }
    }
}

/// A program that returns what it read out of a temporary, or chose between
/// two of, and then calls a function whose temporaries are where what was
/// released lay, before it reads what it was given back.
fn reads(g: &mut Gen) -> Case {
    let mut source = READS.to_string();
    let mut calls = String::new();
    let mut shows = String::new();
    let mut stdout = Vec::new();
    for k in 0..1 + g.below(4) {
        let what = match g.below(5) {
            0 => Read::Element { n: 1 + g.below(6) as u64, at: g.below(8) as u64 },
            1 => Read::Field { n: 1 + g.below(6) as u64, at: g.below(8) as u64 },
            2 => Read::Chosen { n: g.below(8) as u64 },
            3 => Read::Pair { n: g.below(6) as u64 },
            _ => Read::Text { letters: g.below(8) as u64 },
        };
        let part = read(k, what);
        source += &part.item;
        calls += &part.call;
        shows += &part.show;
        stdout.extend(format!("{}\n", part.line).bytes());
    }
    source += &format!(
        "fn main() ! console, alloc {{\n{calls}  z = sum_sq({})\n{shows}}}\n",
        150 + g.below(150)
    );
    Case { source, stdout }
}

fn what_is_read_out_of_a_temporary_and_returned_is_still_there_after_a_call_that_would_overwrite_what_was_released()
 {
    generate::check(313, 40, reads, |case| {
        let program = lower(case.source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
        assert_eq!(scalar::check(&program), Ok(()));
        assert_eq!(scalar::run(&program).stdout, case.stdout);
    });
}

fn every_executable_reads_what_was_returned_out_of_a_temporary_as_the_evaluator_does() {
    let targets = runnable();
    generate::check(314, 15, reads, |case| {
        let program = lower(case.source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
        let said = scalar::run(&scalar::passes(&program));
        assert_eq!(said.stdout, case.stdout);
        for &target in &targets {
            runs_as(case.source.as_bytes(), target, &said);
        }
    });
}
