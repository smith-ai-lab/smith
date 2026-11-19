//! A lambda may give away a value of the function it is written in when it
//! is passed to a parameter that takes its function, and a function that is
//! taken is called once. Each property builds a program from a model, works
//! out from the model where the errors must be and what the program prints,
//! and compares both with the compiler's.

use smith::diag::Pos;
use smith::{check, lower, scalar};

use crate::Test;
use crate::generate::{self, Gen};
use crate::programs::pos_after;

pub const ONCE: &[Test] = &[
    (
        "a_function_that_is_called_once_takes_the_lambda_that_gives_a_value_away_and_one_that_lends_refuses_it",
        a_function_that_is_called_once_takes_the_lambda_that_gives_a_value_away_and_one_that_lends_refuses_it,
    ),
    (
        "a_lambda_that_reads_assigns_or_gives_away_is_refused_exactly_where_the_rules_say_and_prints_what_it_does_in_place",
        a_lambda_that_reads_assigns_or_gives_away_is_refused_exactly_where_the_rules_say_and_prints_what_it_does_in_place,
    ),
    (
        "two_lambdas_of_one_call_that_use_a_name_one_gave_away_are_refused_at_the_second",
        two_lambdas_of_one_call_that_use_a_name_one_gave_away_are_refused_at_the_second,
    ),
    (
        "a_function_that_may_not_call_its_function_is_given_a_lambda_that_gives_away_only_what_may_be_dropped",
        a_function_that_may_not_call_its_function_is_given_a_lambda_that_gives_away_only_what_may_be_dropped,
    ),
    (
        "with_passes_its_block_to_a_parameter_that_takes_it_and_the_block_may_give_a_value_away",
        with_passes_its_block_to_a_parameter_that_takes_it_and_the_block_may_give_a_value_away,
    ),
];

/// A source being written, whose places are those of the text so far.
#[derive(Default)]
struct Src {
    text: String,
}

impl Src {
    fn push(&mut self, text: &str) {
        self.text.push_str(text);
    }

    /// The place where the next text goes.
    fn mark(&self) -> Pos {
        pos_after(&self.text)
    }
}

/// The names of the library the programs of this file use.
const USES: &str = "use std.bits (u64)
use std.console (console, print)
use std.int (wrapping_add)

";

/// The same for a program that adds nothing.
const USES_WITHOUT_ADD: &str = "use std.bits (u64)
use std.console (console, print)

";

/// The type every program of this file gives away, with the functions that
/// lend and take it. The secrets of `a`, `b`, and `s` are 3, 5, and 11.
const KEY: &str = "struct Key {
  secret: u64
}

without Copy for Key

fn sink(k: take Key) -> u64 { k.secret }

fn lend(k: Key) -> u64 { k.secret }

";

/// The errors a program gives, as the places of them, in order.
fn places(source: &str) -> Vec<Pos> {
    let mut found: Vec<Pos> = match check(source.as_bytes()) {
        Ok(_) => Vec::new(),
        Err(errors) => errors.iter().map(|e| e.pos).collect(),
    };
    found.sort();
    found
}

fn printed(source: &str) -> String {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{source}"));
    String::from_utf8(scalar::run(&program).stdout).expect("text")
}

/// What `main` of a program prints for a function `user` of three keys.
const MAIN: &str = "
fn main() ! console, alloc {
  print(\"{user(Key(secret: 3), Key(secret: 5), Key(secret: 11))}\")
}
";

const SECRETS: [u64; 3] = [3, 5, 11];

/// The first program of `take.md`, with a key where the page has a list: the
/// function that takes its function accepts the lambda that gives `acc`
/// away, and the one that lends it refuses it, at the lambda, saying so.
fn a_function_that_is_called_once_takes_the_lambda_that_gives_a_value_away_and_one_that_lends_refuses_it()
 {
    let program = |marked: &str| {
        let mut src = Src::default();
        src.push(USES);
        src.push(KEY);
        src.push(&format!(
            "fn finish(k: take Key, f: {marked}fn(take Key) -> u64) -> u64 {{ f(k) }}\n\n"
        ));
        src.push("fn user(acc: take Key, k: take Key, s: take Key) -> u64 {\n  r = finish(k) ");
        let lambda = src.mark();
        src.push("fn(y) { wrapping_add(sink(acc), sink(y)) }\n  wrapping_add(r, sink(s))\n}\n");
        src.push(MAIN);
        (src.text, lambda)
    };
    let (taken, _) = program("take ");
    assert!(places(&taken).is_empty(), "{:?}\n{taken}", places(&taken));
    assert_eq!(printed(&taken), "19\n", "{taken}");
    let (lent, lambda) = program("");
    assert_eq!(places(&lent), vec![lambda], "{lent}");
    let found = check(lent.as_bytes()).expect_err("refused");
    assert_eq!(found.len(), 1, "{found:?}");
    assert!(found[0].message.contains("`acc`"), "{}", found[0]);
    assert!(
        found[0].message.contains("more than once"),
        "{}",
        found[0]
    );
}

// A function that takes a function, and a lambda for it.

#[derive(Clone, Copy, Debug, PartialEq)]
enum Kind {
    Lent,
    Taken,
}

/// How the function calls what it is given.
#[derive(Clone, Copy, Debug, PartialEq)]
enum Shape {
    Zero,
    One,
    Two,
    /// One call on one path of an `if`.
    IfOne,
    /// One call on each path of an `if`.
    IfBoth,
    /// A call in each round of a loop that runs twice.
    Loop,
    /// The function is passed to a function that lends its function.
    ToLent,
    /// The function is passed to a function that takes its function.
    ToTaken,
}

const SHAPES: [Shape; 8] = [
    Shape::Zero,
    Shape::One,
    Shape::Two,
    Shape::IfOne,
    Shape::IfBoth,
    Shape::Loop,
    Shape::ToLent,
    Shape::ToTaken,
];

/// What a lambda does with the names around it, which are the keys `a` and
/// `b` (0 and 1) and the `var` `n`.
#[derive(Clone, Copy, Debug, PartialEq)]
enum Op {
    Read(usize),
    Give(usize),
    Assign,
}

#[derive(Clone, Debug)]
struct Case {
    kind: Kind,
    shape: Shape,
    cond: bool,
    ops: Vec<Op>,
    /// The key that is read after the call, if any.
    after: Option<usize>,
}

fn cases(g: &mut Gen) -> Case {
    let ops = (0..g.below(4))
        .map(|_| match g.below(3) {
            0 => Op::Read(g.below(2)),
            1 => Op::Give(g.below(2)),
            _ => Op::Assign,
        })
        .collect();
    Case {
        kind: if g.one_in(2) {
            Kind::Lent
        } else {
            Kind::Taken
        },
        shape: *g.pick(&SHAPES),
        cond: g.one_in(2),
        ops,
        after: g.one_in(3).then(|| g.below(2)),
    }
}

const NAMES: [&str; 2] = ["a", "b"];

impl Case {
    fn assigns(&self) -> bool {
        self.ops.contains(&Op::Assign)
    }

    /// What each call of the function gives its function, in order.
    fn calls(&self) -> Vec<u64> {
        match self.shape {
            Shape::Zero => vec![],
            Shape::One | Shape::ToLent | Shape::ToTaken => vec![7],
            Shape::Two => vec![7, 7],
            Shape::IfOne if self.cond => vec![7],
            Shape::IfOne => vec![],
            Shape::IfBoth => vec![if self.cond { 7 } else { 8 }],
            Shape::Loop => vec![0, 1],
        }
    }

    /// How many lines assign the `var`, the expression the lambda gives for
    /// the argument `y`, and each use of a key in it, in the order written:
    /// the key, and whether it is given away.
    fn body(&self, y: &str) -> (usize, String, Vec<(usize, bool)>) {
        let assigns = self.ops.iter().filter(|op| **op == Op::Assign).count();
        let mut expr = String::from(y);
        let mut gives = Vec::new();
        for op in &self.ops {
            match op {
                Op::Read(k) => {
                    expr = format!("wrapping_add({expr}, lend({}))", NAMES[*k]);
                    gives.push((*k, false));
                }
                Op::Give(k) => {
                    expr = format!("wrapping_add({expr}, sink({}))", NAMES[*k]);
                    gives.push((*k, true));
                }
                Op::Assign => {}
            }
        }
        (assigns, expr, gives)
    }
}

/// Writes `expr`, noting the place of the name of each use of a key.
fn write_expr(src: &mut Src, expr: &str, uses: &[(usize, bool)], at: &mut Vec<(usize, bool, Pos)>) {
    let mut rest = expr;
    let mut uses = uses.iter();
    while let Some(found) = rest.find("sink(").into_iter().chain(rest.find("lend(")).min() {
        src.push(&rest[..found + "sink(".len()]);
        let (key, give) = *uses.next().expect("a use");
        at.push((key, give, src.mark()));
        rest = &rest[found + "sink(".len()..];
    }
    src.push(rest);
}

/// Whether a use, which is a give, is the first of its key to be given
/// away, as the others are used after it.
fn first_give(at: &[(usize, bool, Pos)], i: usize) -> bool {
    at[i].1 && !at[..i].iter().any(|(k, give, _)| *k == at[i].0 && *give)
}

/// The places of the uses of a key that come after it was given away.
fn after_gives(at: &[(usize, bool, Pos)]) -> Vec<Pos> {
    (at.iter().enumerate())
        .filter(|(i, (key, _, _))| at[..*i].iter().any(|(k, give, _)| k == key && *give))
        .map(|(_, (_, _, pos))| *pos)
        .collect()
}

/// The places of a case's errors, and the program.
fn program(case: &Case) -> (String, Vec<Pos>) {
    let mut src = Src::default();
    let mut wanted: Vec<Pos> = Vec::new();
    src.push(USES);
    src.push(KEY);
    let (shape, kind) = (case.shape, case.kind);
    let marked = if kind == Kind::Taken { "take " } else { "" };
    if shape == Shape::ToLent {
        src.push("fn into_lent(f: fn(u64) -> u64) -> u64 { f(7) }\n\n");
    }
    if shape == Shape::ToTaken {
        src.push("fn into_taken(f: take fn(u64) -> u64) -> u64 { f(7) }\n\n");
    }
    src.push(&format!("fn recv(c: bool, f: {marked}fn(u64) -> u64) -> u64 {{\n"));
    match shape {
        Shape::Zero => src.push("  0\n"),
        Shape::One => src.push("  f(7)\n"),
        Shape::Two => {
            src.push("  wrapping_add(f(7), ");
            let second = src.mark();
            src.push("f(7))\n");
            if kind == Kind::Taken {
                wanted.push(second);
            }
        }
        Shape::IfOne => src.push("  if c { f(7) } else { 0 }\n"),
        Shape::IfBoth => src.push("  if c { f(7) } else { f(8) }\n"),
        Shape::Loop => {
            src.push("  var t: u64 = 0\n  loop i in 0..2 {\n    t = wrapping_add(t, ");
            let call = src.mark();
            src.push("f(i))\n  }\n  t\n");
            if kind == Kind::Taken {
                wanted.push(call);
            }
        }
        Shape::ToLent | Shape::ToTaken => {
            let callee = if shape == Shape::ToLent {
                "into_lent"
            } else {
                "into_taken"
            };
            src.push(&format!("  {callee}("));
            let arg = src.mark();
            src.push("f)\n");
            // A function that is lent is not given to a parameter that takes
            // it, and one that is called once is not given to one that lends.
            if (kind == Kind::Taken) == (shape == Shape::ToLent) {
                wanted.push(arg);
            }
        }
    }
    src.push("}\n\n");
    src.push("fn user(a: take Key, b: take Key, s: take Key) -> u64 {\n");
    if case.assigns() {
        src.push("  var n: u64 = 0\n");
    }
    src.push("  x = lend(a)\n");
    src.push(&format!("  r = recv({}) ", case.cond));
    let lambda = src.mark();
    src.push("fn(y) {\n");
    let mut uses_at = Vec::new();
    let (assigns, expr, uses) = case.body("y");
    for _ in 0..assigns {
        src.push("    n = wrapping_add(n, y)\n");
    }
    src.push("    ");
    write_expr(&mut src, &expr, &uses, &mut uses_at);
    src.push("\n  }\n");
    // A lambda that gives a value away is called once.
    if kind == Kind::Lent && (0..uses_at.len()).any(|i| first_give(&uses_at, i)) {
        wanted.push(lambda);
    }
    let mut sum = String::from("wrapping_add(x, r)");
    if case.assigns() {
        sum = format!("wrapping_add({sum}, n)");
    }
    if let Some(k) = case.after {
        src.push("  z = lend(");
        let at = src.mark();
        src.push(&format!("{})\n", NAMES[k]));
        uses_at.push((k, false, at));
        sum = format!("wrapping_add({sum}, z)");
    }
    // A key is used after it was given away, in the lambda or after it.
    wanted.extend(after_gives(&uses_at));
    src.push(&format!("  wrapping_add({sum}, sink(s))\n}}\n"));
    src.push(MAIN);
    wanted.sort();
    wanted.dedup();
    (src.text, wanted)
}

/// What the program prints, worked out from the case, and the program with
/// the lambda written where each call of it is.
fn in_place(case: &Case) -> (u64, String) {
    let mut n = 0;
    let mut results = Vec::new();
    for y in case.calls() {
        let mut result = y;
        for op in &case.ops {
            match op {
                Op::Assign => n += y,
                Op::Read(k) | Op::Give(k) => result += SECRETS[*k],
            }
        }
        results.push(result);
    }
    let r: u64 = results.iter().sum();
    let mut value = SECRETS[0] + r + SECRETS[2];
    if case.assigns() {
        value += n;
    }
    if let Some(k) = case.after {
        value += SECRETS[k];
    }

    let mut src = Src::default();
    src.push(USES);
    src.push(KEY);
    src.push("fn user(a: take Key, b: take Key, s: take Key) -> u64 {\n");
    if case.assigns() {
        src.push("  var n: u64 = 0\n");
    }
    src.push("  x = lend(a)\n");
    for (j, y) in case.calls().iter().enumerate() {
        src.push(&format!("  y{j}: u64 = {y}\n"));
        let (assigns, expr, _) = case.body(&format!("y{j}"));
        for _ in 0..assigns {
            src.push(&format!("  n = wrapping_add(n, y{j})\n"));
        }
        src.push(&format!("  r{j} = {expr}\n"));
    }
    let mut sum = match results.len() {
        0 => String::from("0"),
        _ => (1..results.len()).fold(String::from("r0"), |s, j| format!("wrapping_add({s}, r{j})")),
    };
    sum = format!("wrapping_add(x, {sum})");
    if case.assigns() {
        sum = format!("wrapping_add({sum}, n)");
    }
    if let Some(k) = case.after {
        src.push(&format!("  z = lend({})\n", NAMES[k]));
        sum = format!("wrapping_add({sum}, z)");
    }
    src.push(&format!("  wrapping_add({sum}, sink(s))\n}}\n"));
    src.push(MAIN);
    (value, src.text)
}

fn a_lambda_that_reads_assigns_or_gives_away_is_refused_exactly_where_the_rules_say_and_prints_what_it_does_in_place()
 {
    generate::check(706, 70, cases, |case| {
        let (source, wanted) = program(case);
        let got = places(&source);
        assert_eq!(got, wanted, "{case:?}\n{source}");
        if wanted.is_empty() {
            let (value, twin) = in_place(case);
            assert_eq!(printed(&source), format!("{value}\n"), "{case:?}\n{source}");
            assert_eq!(printed(&twin), format!("{value}\n"), "{case:?}\n{twin}");
        }
    });
}

// Two lambdas of one call.

#[derive(Clone, Debug)]
struct Pair {
    kind: Kind,
    /// What each lambda does: nothing, reads a key, or gives it away.
    first: Option<Op>,
    second: Option<Op>,
}

fn pairs(g: &mut Gen) -> Pair {
    let op = |g: &mut Gen| match g.below(3) {
        0 => None,
        1 => Some(Op::Read(g.below(2))),
        _ => Some(Op::Give(g.below(2))),
    };
    Pair {
        kind: if g.one_in(2) {
            Kind::Lent
        } else {
            Kind::Taken
        },
        first: op(g),
        second: op(g),
    }
}

fn two_lambdas_of_one_call_that_use_a_name_one_gave_away_are_refused_at_the_second() {
    generate::check(707, 60, pairs, |pair| {
        let marked = if pair.kind == Kind::Taken { "take " } else { "" };
        let mut src = Src::default();
        src.push(USES);
        src.push(KEY);
        src.push(&format!(
            "fn both(f: {marked}fn() -> u64, g: {marked}fn() -> u64) -> u64 {{ wrapping_add(f(), g()) }}\n\nfn user(a: take Key, b: take Key, s: take Key) -> u64 {{\n  r = both(f: "
        ));
        let mut wanted = Vec::new();
        let mut uses_at: Vec<(usize, bool, Pos)> = Vec::new();
        let mut value = 0;
        for (i, op) in [pair.first, pair.second].into_iter().enumerate() {
            if i == 1 {
                src.push(") ");
            }
            let block = src.mark();
            src.push("{ ");
            match op {
                None => {
                    src.push("1");
                    value += 1;
                }
                Some(Op::Read(k)) => {
                    src.push("lend(");
                    uses_at.push((k, false, src.mark()));
                    src.push(&format!("{})", NAMES[k]));
                    value += SECRETS[k];
                }
                Some(Op::Give(k)) => {
                    src.push("sink(");
                    uses_at.push((k, true, src.mark()));
                    src.push(&format!("{})", NAMES[k]));
                    value += SECRETS[k];
                    if pair.kind == Kind::Lent && first_give(&uses_at, uses_at.len() - 1) {
                        wanted.push(block);
                    }
                }
                Some(Op::Assign) => unreachable!("not generated"),
            }
            src.push(" }");
        }
        wanted.extend(after_gives(&uses_at));
        src.push("\n  wrapping_add(r, sink(s))\n}\n");
        src.push(MAIN);
        wanted.sort();
        wanted.dedup();
        let got = places(&src.text);
        assert_eq!(got, wanted, "{pair:?}\n{}", src.text);
        if wanted.is_empty() {
            assert_eq!(printed(&src.text), format!("{}\n", value + SECRETS[2]), "{}", src.text);
        }
    });
}

// `with`.

/// A block that is passed with `with` to a function that takes it gives a
/// value away, and one that lends it is refused.
fn with_passes_its_block_to_a_parameter_that_takes_it_and_the_block_may_give_a_value_away() {
    let program = |marked: &str| {
        let mut src = Src::default();
        src.push(USES);
        src.push(KEY);
        src.push(&format!(
            "fn spend(k: take Key, body: {marked}fn(take Key) -> u64) -> u64 {{ body(k) }}\n\nfn user(a: take Key, b: take Key, s: take Key) -> u64 {{\n  "
        ));
        let at = src.mark();
        src.push("with y = spend(a)\n  wrapping_add(sink(b), wrapping_add(sink(y), sink(s)))\n}\n");
        src.push(MAIN);
        (src.text, at)
    };
    let (taken, _) = program("take ");
    assert!(places(&taken).is_empty(), "{:?}\n{taken}", places(&taken));
    assert_eq!(printed(&taken), "19\n", "{taken}");
    let (lent, at) = program("");
    assert_eq!(places(&lent), vec![at], "{lent}");
}

/// A function that takes its function may not call it, so a lambda that
/// gives away a value of a type parameter is refused unless the parameter
/// has `Discard`, and the function that does not call it is accepted.
fn a_function_that_may_not_call_its_function_is_given_a_lambda_that_gives_away_only_what_may_be_dropped()
 {
    let program = |bound: &str| {
        let mut src = Src::default();
        src.push(USES_WITHOUT_ADD);
        src.push(KEY);
        src.push("fn run[R](c: bool, other: take R, f: take fn(u64) -> R) -> R {\n  other\n}\n\n");
        src.push("fn keep[T](x: take T) -> T { x }\n\n");
        src.push(&format!("fn user[T{bound}](x: take T, z: take T) -> T {{\n  "));
        src.push("run(false, z) ");
        let at = src.mark();
        src.push("fn(y) { keep(x) }\n}\n\n");
        src.push("fn main() ! console, alloc {\n  print(\"{sink(user(Key(secret: 3), Key(secret: 4)))}\")\n}\n");
        (src.text, at)
    };
    let (discarding, _) = program(": Discard");
    assert!(places(&discarding).is_empty(), "{:?}\n{discarding}", places(&discarding));
    assert_eq!(printed(&discarding), "4\n", "{discarding}");
    let (plain, at) = program("");
    assert_eq!(places(&plain), vec![at], "{plain}");
    let found = check(plain.as_bytes()).expect_err("refused");
    assert!(found[0].message.contains("`x`"), "{}", found[0]);
    assert!(found[0].message.contains("Discard"), "{}", found[0]);
}
