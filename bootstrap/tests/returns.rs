//! Properties of `return`: it leaves the nearest function or lambda with a
//! value, from an `if`, a `match`, a `loop`, and blocks inside them.
//!
//! The generator builds a function out of a model of its lines, and works the
//! answer out by running the model in Rust, so it shares nothing with the
//! compiler. For a model with no `loop`, it also writes the function a second
//! way, with each `return e` as the value of an `if` that skips the rest, as
//! the page of `return` says it means.

use smith::diag::Error;
use smith::scalar::{self, Op, Outcome};
use smith::{check, compile, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::numbers::INTS;
use crate::programs::pos_after;
use crate::targets;

pub const ONCE: &[Test] = &[
    (
        "a_function_with_returns_prints_what_it_prints_with_each_return_as_the_value_of_an_if",
        a_function_with_returns_prints_what_it_prints_with_each_return_as_the_value_of_an_if,
    ),
    (
        "a_return_in_a_loop_ends_the_loop_and_the_function",
        a_return_in_a_loop_ends_the_loop_and_the_function,
    ),
    (
        "a_return_in_a_lambda_leaves_the_lambda_and_the_caller_goes_on",
        a_return_in_a_lambda_leaves_the_lambda_and_the_caller_goes_on,
    ),
    (
        "a_return_in_the_rest_of_a_block_after_try_leaves_with_its_value",
        a_return_in_the_rest_of_a_block_after_try_leaves_with_its_value,
    ),
    (
        "the_examples_of_the_page_give_what_it_says",
        the_examples_of_the_page_give_what_it_says,
    ),
    (
        "a_return_is_refused_exactly_where_its_value_does_not_fit_or_a_line_cannot_run",
        a_return_is_refused_exactly_where_its_value_does_not_fit_or_a_line_cannot_run,
    ),
    (
        "after_an_if_that_returns_arith_knows_the_condition_is_false",
        after_an_if_that_returns_arith_knows_the_condition_is_false,
    ),
    (
        "a_return_costs_a_jump_and_a_last_return_is_the_value_it_gives",
        a_return_costs_a_jump_and_a_last_return_is_the_value_it_gives,
    ),
    (
        "a_value_without_copy_is_given_away_by_a_return_and_a_lent_value_is_not_returned",
        a_value_without_copy_is_given_away_by_a_return_and_a_lent_value_is_not_returned,
    ),
];

pub const MACHINE: &[Test] = &[(
    "every_executable_of_a_program_with_return_does_what_the_evaluator_says",
    every_executable_of_a_program_with_return_does_what_the_evaluator_says,
)];

/// What a `return` or the end of a function gives.
#[derive(Clone, Debug)]
enum Val {
    Acc,
    Param,
    Lit(i64),
    /// `wrapping_add(acc, k)`.
    Plus(i64),
}

/// A test of the parameter or of `acc`.
#[derive(Clone, Debug)]
struct Cond {
    on_acc: bool,
    /// 0 for `<`, 1 for `>`, 2 for `==`.
    op: usize,
    k: i64,
}

/// A line of a function whose parameter is `x` and that has `var acc`.
#[derive(Clone, Debug)]
enum Item {
    /// `acc = wrapping_add(acc, k)`.
    Bump(i64),
    /// `return e`, which ends its list of lines.
    Return(Val),
    /// `if c { return e }`.
    Early(Cond, Val),
    Branch(Cond, Vec<Item>, Vec<Item>),
    /// `match x` with a block for 0, one for 1, and one for the rest.
    Pick(Vec<Item>, Vec<Item>, Vec<Item>),
    /// `loop i in 0..n`.
    Loop(i64, Vec<Item>),
}

impl Val {
    fn eval(&self, x: i64, acc: i64) -> i64 {
        match self {
            Val::Acc => acc,
            Val::Param => x,
            Val::Lit(k) => *k,
            Val::Plus(k) => acc.wrapping_add(*k),
        }
    }

    fn write(&self, p: &str) -> String {
        match self {
            Val::Acc => "acc".into(),
            Val::Param => p.into(),
            Val::Lit(k) => k.to_string(),
            Val::Plus(k) => format!("wrapping_add(acc, {k})"),
        }
    }
}

impl Cond {
    fn holds(&self, x: i64, acc: i64) -> bool {
        let v = if self.on_acc { acc } else { x };
        match self.op {
            0 => v < self.k,
            1 => v > self.k,
            _ => v == self.k,
        }
    }

    fn write(&self, p: &str) -> String {
        let v = if self.on_acc { "acc" } else { p };
        let op = ["<", ">", "=="][self.op];
        format!("{v} {op} {}", self.k)
    }
}

fn val(g: &mut Gen) -> Val {
    match g.below(4) {
        0 => Val::Lit(g.below(7) as i64 - 3),
        1 => Val::Acc,
        2 => Val::Param,
        _ => Val::Plus(g.below(7) as i64 - 3),
    }
}

fn cond(g: &mut Gen) -> Cond {
    Cond {
        on_acc: g.one_in(2),
        op: g.below(3),
        k: g.below(5) as i64,
    }
}

/// A list of lines, which has a `return` only as its last, and has a `loop`
/// only when `loops` says so. It is never empty.
fn items(g: &mut Gen, depth: usize, loops: bool) -> Vec<Item> {
    let mut out = Vec::new();
    for _ in 0..1 + g.below(3) {
        let kinds = if depth == 0 { 3 } else { 6 };
        out.push(match g.below(kinds) {
            0 => Item::Bump(g.below(7) as i64 - 3),
            1 => Item::Early(cond(g), val(g)),
            2 => Item::Bump(g.below(7) as i64 - 3),
            3 => Item::Branch(
                cond(g),
                items(g, depth - 1, loops),
                items(g, depth - 1, loops),
            ),
            4 => Item::Pick(
                items(g, depth - 1, loops),
                items(g, depth - 1, loops),
                items(g, depth - 1, loops),
            ),
            _ if loops => Item::Loop(g.below(4) as i64, items(g, depth - 1, loops)),
            _ => Item::Early(cond(g), val(g)),
        });
    }
    if g.one_in(4) {
        out.push(Item::Return(val(g)));
    }
    out
}

/// Whether every way through the lines ends in a `return`.
fn always_returns(items: &[Item]) -> bool {
    items.iter().any(|item| match item {
        Item::Return(_) => true,
        Item::Branch(_, a, b) => always_returns(a) && always_returns(b),
        Item::Pick(a, b, rest) => always_returns(a) && always_returns(b) && always_returns(rest),
        _ => false,
    })
}

/// The lines without those that nothing reaches, which are the ones after a
/// line that always returns. The rewrite has no such lines to hold.
fn reached(items: Vec<Item>) -> Vec<Item> {
    let mut out = Vec::new();
    for item in items {
        let item = match item {
            Item::Branch(c, a, b) => Item::Branch(c, reached(a), reached(b)),
            Item::Pick(a, b, rest) => Item::Pick(reached(a), reached(b), reached(rest)),
            Item::Loop(n, body) => Item::Loop(n, reached(body)),
            other => other,
        };
        let done = always_returns(std::slice::from_ref(&item));
        out.push(item);
        if done {
            break;
        }
    }
    out
}

/// What the lines give when they return, with `acc` as they leave it.
fn exec(items: &[Item], x: i64, acc: &mut i64) -> Option<i64> {
    for item in items {
        let arm = match item {
            Item::Bump(k) => {
                *acc = acc.wrapping_add(*k);
                continue;
            }
            Item::Return(v) => return Some(v.eval(x, *acc)),
            Item::Early(c, v) => {
                if c.holds(x, *acc) {
                    return Some(v.eval(x, *acc));
                }
                continue;
            }
            Item::Branch(c, a, b) => {
                if c.holds(x, *acc) {
                    a
                } else {
                    b
                }
            }
            Item::Pick(a, b, rest) => match x {
                0 => a,
                1 => b,
                _ => rest,
            },
            Item::Loop(n, body) => {
                for _ in 0..*n {
                    if let Some(v) = exec(body, x, acc) {
                        return Some(v);
                    }
                }
                continue;
            }
        };
        if let Some(v) = exec(arm, x, acc) {
            return Some(v);
        }
    }
    None
}

/// What a function made of the lines gives for `x`.
fn gives(items: &[Item], x: i64) -> i64 {
    let mut acc = 0;
    exec(items, x, &mut acc).unwrap_or(acc)
}

fn ends_in_return(items: &[Item]) -> bool {
    matches!(items.last(), Some(Item::Return(_)))
}

/// The lines as they are written, with `return`s, the parameter being `p`.
fn write(items: &[Item], p: &str) -> String {
    let mut out = String::new();
    for item in items {
        match item {
            Item::Bump(k) => out += &format!("acc = wrapping_add(acc, {k})\n"),
            Item::Return(v) => out += &format!("return {}\n", v.write(p)),
            Item::Early(c, v) => {
                out += &format!("if {} {{ return {} }}\n", c.write(p), v.write(p))
            }
            Item::Branch(c, a, b) => {
                out += &format!(
                    "if {} {{\n{}}} else {{\n{}}}\n",
                    c.write(p),
                    write(a, p),
                    write(b, p)
                )
            }
            Item::Pick(a, b, rest) => {
                out += &format!(
                    "match {p} {{\n0 {{\n{}}}\n1 {{\n{}}}\n{} {{\n{}}}\n}}\n",
                    write(a, p),
                    write(b, p),
                    rest_name(),
                    write(rest, p)
                )
            }
            Item::Loop(n, body) => {
                out += &format!("loop {} in 0..{n} {{\n{}}}\n", rest_name(), write(body, p))
            }
        }
    }
    out
}

/// The body of a function made of the lines: what it gives is the last line,
/// or `acc` when the lines do not end in a `return`.
fn body(items: &[Item], p: &str) -> String {
    let tail = if ends_in_return(items) { "" } else { "acc\n" };
    format!("var acc: i64 = 0\n{}{tail}", write(items, p))
}

/// The same lines with no `return`: each is the value of an `if` that holds
/// the lines that come after it where it does not run, and a block that ends
/// holds the lines that follow the `if` or `match` it is in. It is for lines
/// with no `loop`.
fn as_value(items: &[Item], rest: &[Item], p: &str) -> String {
    let Some((first, more)) = items.split_first() else {
        return if rest.is_empty() {
            "acc\n".into()
        } else {
            as_value(rest, &[], p)
        };
    };
    let after = |block: &[Item]| [block, more, rest].concat();
    match first {
        Item::Bump(k) => format!(
            "acc = wrapping_add(acc, {k})\n{}",
            as_value(more, rest, p)
        ),
        Item::Return(v) => format!("{}\n", v.write(p)),
        Item::Early(c, v) => format!(
            "if {} {{ {} }} else {{\n{}}}\n",
            c.write(p),
            v.write(p),
            as_value(more, rest, p)
        ),
        Item::Branch(c, a, b) => format!(
            "if {} {{\n{}}} else {{\n{}}}\n",
            c.write(p),
            as_value(&after(a), &[], p),
            as_value(&after(b), &[], p)
        ),
        Item::Pick(a, b, other) => format!(
            "match {p} {{\n0 {{\n{}}}\n1 {{\n{}}}\n{} {{\n{}}}\n}}\n",
            as_value(&after(a), &[], p),
            as_value(&after(b), &[], p),
            rest_name(),
            as_value(&after(other), &[], p)
        ),
        Item::Loop(..) => unreachable!("a rewrite has no loop"),
    }
}

thread_local! {
    static NAMES: std::cell::Cell<usize> = const { std::cell::Cell::new(0) };
}

/// A name for the block of a `match` that covers the rest, which no other
/// name in the function is.
fn rest_name() -> String {
    NAMES.with(|n| {
        n.set(n.get() + 1);
        format!("rest{}", n.get())
    })
}

const ARGUMENTS: [i64; 4] = [0, 1, 2, 5];

/// A program whose `f` is the function, called for each of `ARGUMENTS`.
fn program(function: &str) -> String {
    let calls: Vec<String> = ARGUMENTS.iter().map(|x| format!("{{f({x})}}")).collect();
    format!(
        "fn f(x: i64) -> i64 {{\n{function}}}\n\nfn main() ! console, alloc {{\n  print(\"{}\")\n}}\n",
        calls.join(" ")
    )
}

fn says(lines: &str) -> Outcome {
    Outcome {
        stdout: lines.as_bytes().to_vec(),
        status: 0,
    }
}

/// What the program of the lines prints.
fn printed(items: &[Item]) -> Outcome {
    let words: Vec<String> = ARGUMENTS.iter().map(|&x| gives(items, x).to_string()).collect();
    says(&format!("{}\n", words.join(" ")))
}

fn evaluate(source: &str) -> Outcome {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    assert_eq!(scalar::check(&program), Ok(()), "for {source}");
    assert_eq!(scalar::check(&scalar::passes(&program)), Ok(()), "for {source}");
    scalar::run(&program)
}

fn prints(source: &str, lines: &[&str]) {
    let stdout: String = lines.iter().map(|l| format!("{l}\n")).collect();
    assert_eq!(evaluate(source), says(&stdout), "for {source}");
}

/// The errors a program is refused with, or none if it compiles.
fn refused(source: &str) -> Vec<Error> {
    check(source.as_bytes()).err().unwrap_or_default()
}

fn a_function_with_returns_prints_what_it_prints_with_each_return_as_the_value_of_an_if() {
    generate::check(
        61,
        12,
        |g| items(g, 2, false),
        |lines| {
            let said = printed(lines);
            let returned = program(&body(lines, "x"));
            assert_eq!(evaluate(&returned), said, "for {returned}");
            let rewritten = program(&format!("var acc: i64 = 0\n{}", as_value(lines, &[], "x")));
            assert!(!rewritten.contains("return"), "{rewritten}");
            assert_eq!(evaluate(&rewritten), said, "for {rewritten}");
        },
    );
}

fn a_return_in_a_loop_ends_the_loop_and_the_function() {
    generate::check(
        62,
        12,
        |g| items(g, 2, true),
        |lines| {
            let source = program(&body(lines, "x"));
            assert_eq!(evaluate(&source), printed(lines), "for {source}");
        },
    );
}

/// A call of a function that takes a lambda, with the lines in the lambda:
/// `call` calls it twice and goes on with what the second call gave, and
/// the caller goes on after the call.
fn lambda_program(lines: &[Item]) -> String {
    let calls: Vec<String> = ARGUMENTS.iter().map(|x| format!("{{caller({x})}}")).collect();
    format!(
        "fn call(x: i64, f: fn(i64) -> i64) -> i64 {{\n  a = f(x)\n  b = f(wrapping_add(x, 1))\n  wrapping_add(wrapping_mul(a, 3), b)\n}}\n\n\
         fn caller(x: i64) -> i64 {{\n  r = call(x) fn(v) {{\n{}  }}\n  wrapping_add(r, 7)\n}}\n\n\
         fn main() ! console, alloc {{\n  print(\"{}\")\n}}\n",
        body(lines, "v"),
        calls.join(" ")
    )
}

fn lambda_printed(lines: &[Item]) -> Outcome {
    let words: Vec<String> = ARGUMENTS
        .iter()
        .map(|&x| {
            let a = gives(lines, x);
            let b = gives(lines, x + 1);
            a.wrapping_mul(3).wrapping_add(b).wrapping_add(7).to_string()
        })
        .collect();
    says(&format!("{}\n", words.join(" ")))
}

fn a_return_in_a_lambda_leaves_the_lambda_and_the_caller_goes_on() {
    generate::check(
        63,
        12,
        |g| items(g, 2, true),
        |lines| {
            let source = lambda_program(lines);
            assert_eq!(evaluate(&source), lambda_printed(lines), "for {source}");
        },
    );
}

fn a_return_in_the_rest_of_a_block_after_try_leaves_with_its_value() {
    let source = "fn find(name: str, have: Option[str]) -> Result[str, str] {\n  match have {\n    .some(value) { .ok(value) }\n    .none { .err(name) }\n  }\n}\n\n\
        fn both(a: Option[str], b: Option[str]) -> Result[str, str] {\n  x = try find(\"a\", a)\n  if x == \"stop\" { return .err(\"stopped\") }\n  y = try find(\"b\", b)\n  .ok(y)\n}\n\n\
        fn show(r: Result[str, str]) -> str ! alloc {\n  match r {\n    .ok(v) { \"ok {v}\" }\n    .err(e) { \"err {e}\" }\n  }\n}\n\n\
        fn main() ! console, alloc {\n  print(show(both(.some(\"1\"), .some(\"2\"))))\n  print(show(both(.some(\"stop\"), .some(\"2\"))))\n  print(show(both(.none, .some(\"2\"))))\n  print(show(both(.some(\"1\"), .none)))\n}\n";
    prints(source, &["ok 2", "err stopped", "err a", "err b"]);
}

/// The examples of the page that need nothing but what is built.
fn the_examples_of_the_page_give_what_it_says() {
    let classify = "fn classify(b: u8) -> str {\n  if b >= '0' and b <= '9' { return \"digit\" }\n  if b == ' ' { return \"space\" }\n  \"other\"\n}\n\n\
        fn classified(b: u8) -> str {\n  if b >= '0' and b <= '9' { \"digit\" } else if b == ' ' { \"space\" } else { \"other\" }\n}\n\n\
        fn main() ! console, alloc {\n  print(classify('7'))\n  print(classify(' '))\n  print(classify('x'))\n  print(classified('7'))\n  print(classified(' '))\n  print(classified('x'))\n}\n";
    prints(
        classify,
        &["digit", "space", "other", "digit", "space", "other"],
    );
    let log = "fn log_if(verbose: bool, text: str) ! console {\n  if not verbose { return }\n  print(text)\n}\n\n\
        fn main() ! console, alloc {\n  log_if(false, \"x\")\n  log_if(true, \"y\")\n}\n";
    prints(log, &["y"]);
    let refused_classify = "fn classify(b: u8) -> str {\n  if b == ' ' { return 1 }\n  \"other\"\n}\n\nfn main() ! console, alloc {\n  print(classify('x'))\n}\n";
    assert_eq!(refused(refused_classify).len(), 1);
}

/// Where a mistake is, and the program with it: `at` is what comes before it.
fn placed(before: &str, after: &str) -> (String, smith::diag::Pos) {
    (format!("{before}{after}"), pos_after(before))
}

fn a_return_is_refused_exactly_where_its_value_does_not_fit_or_a_line_cannot_run() {
    let main = "\nfn main() ! console, alloc {\n  print(\"{f(1)}\")\n}\n";
    let cases = [
        // A value of another type than the result.
        placed("fn f(x: i64) -> i64 {\n  if x > 1 { return ", &format!("\"no\" }}\n  x\n}}\n{main}")),
        // Nothing, where the function gives a value.
        placed("fn f(x: i64) -> i64 {\n  if x > 1 { ", &format!("return }}\n  x\n}}\n{main}")),
        // A value, where the function gives nothing.
        placed(
            "fn f(x: i64) ! console {\n  if x > 1 { return ",
            "x }\n  print(\"a\")\n}\n\nfn main() ! console, alloc {\n  f(1)\n}\n",
        ),
        // A line after a `return` in its block, at its first line.
        placed(
            "fn f(x: i64) -> i64 {\n  if x > 1 {\n    return 1\n    ",
            &format!("2\n  }}\n  x\n}}\n{main}"),
        ),
        placed(
            "fn f(x: i64) -> i64 {\n  return 1\n  ",
            &format!("x\n}}\n{main}"),
        ),
    ];
    for (source, pos) in cases {
        let errors = refused(&source);
        assert_eq!(errors.len(), 1, "{errors:?} for {source}");
        assert_eq!(errors[0].pos, pos, "{errors:?} for {source}");
    }
    // The same functions with the mistake put right compile.
    for fixed in [
        "fn f(x: i64) -> i64 {\n  if x > 1 { return 2 }\n  x\n}\n",
        "fn f(x: i64) -> i64 {\n  if x > 1 { return x }\n  1\n}\n",
    ] {
        let source = format!("{fixed}{main}");
        assert_eq!(refused(&source).len(), 0, "for {source}");
    }
}

fn after_an_if_that_returns_arith_knows_the_condition_is_false() {
    for ty in INTS.iter().filter(|t| !t.signed) {
        let t = ty.name;
        let program = |early: &str, result: &str| {
            format!(
                "fn f(a: {t}, b: {t}) -> {t} {{\n  {early}\n  {result}\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{f(1, 0)}}\")\n}}\n"
            )
        };
        // `a < b` is false after the `if`, as in its `else`.
        let known = program("if a < b { return 0 }", "a - b");
        assert_eq!(refused(&known).len(), 0, "{:?} for {known}", refused(&known));
        let unknown = program("if a > b { return 0 }", "a - b");
        assert_eq!(refused(&unknown).len(), 1, "for {unknown}");
        let as_else = program("", "if a < b { 0 } else { a - b }");
        assert_eq!(refused(&as_else).len(), 0, "for {as_else}");
    }
}

/// How many functions the program holds, and how many calls it makes once
/// what nothing uses is taken away. The calls are counted before the passes
/// that inline and fold, since the rewrite holds the lines after an `if` once
/// for each block of it, and a pass may know more of each copy than it can of
/// the one place the `return`s share.
fn size(source: &str) -> (usize, usize) {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let after = scalar::remove(&program);
    let calls = (after.functions.iter())
        .flat_map(|f| &f.blocks)
        .flat_map(|b| &b.instructions)
        .filter(|i| matches!(i.op, Op::Call { .. }))
        .count();
    (program.functions.len(), calls)
}

fn a_return_costs_a_jump_and_a_last_return_is_the_value_it_gives() {
    generate::check(
        64,
        8,
        |g| reached(items(g, 2, false)),
        |lines| {
            let returned = program(&body(lines, "x"));
            let rewritten = program(&format!("var acc: i64 = 0\n{}", as_value(lines, &[], "x")));
            assert_eq!(evaluate(&returned), evaluate(&rewritten), "for {returned}");
            // The rewrite holds the lines after an `if` once for each block of
            // it, so it may call more, and the `return`s call nothing more.
            let (functions, calls) = size(&returned);
            let (rewritten_functions, rewritten_calls) = size(&rewritten);
            assert_eq!(functions, rewritten_functions, "for {returned}");
            assert!(calls <= rewritten_calls, "for {returned}");
        },
    );
    // A function whose last line is `return e` is the one that ends in `e`.
    generate::check(
        65,
        5,
        |g| (items(g, 1, false), val(g)),
        |(lines, last)| {
            let lines: Vec<Item> = lines
                .iter()
                .filter(|l| !matches!(l, Item::Return(_)))
                .cloned()
                .collect();
            // The function is larger than what a pass inlines, as it was when a
            // `wrapping_` function was a call and not an instruction. Its
            // statements are read by nothing, so no pass removes them first.
            let mut padding = String::from("var padding = x\n");
            for _ in 0..scalar::LEAF_BOUND {
                padding += "padding = wrapping_add(padding, x)\n";
            }
            let with = |end: String| {
                program(&format!("{padding}var acc: i64 = 0\n{}{end}\n", write(&lines, "x")))
            };
            let by_return = with(format!("return {}", last.write("x")));
            let by_value = with(last.write("x"));
            let built = |source: &str| {
                compile(source.as_bytes(), targets::any()).unwrap_or_else(|e| panic!("{e:?}"))
            };
            assert!(built(&by_return) == built(&by_value), "for {by_return}");
        },
    );
}

fn a_value_without_copy_is_given_away_by_a_return_and_a_lent_value_is_not_returned() {
    let program = |function: &str| {
        format!(
            "struct Box {{\n  name: str\n}}\n\nwithout Copy for Box\n\n{function}\nfn main() ! console, alloc {{\n  b = pick(Box(name: \"a\"), true)\n  print(b.name)\n}}\n"
        )
    };
    // A value given away by a `return` is gone only on the path it ends, so
    // the lines after the `if` may give it away again.
    let fine = program(
        "fn pick(b: take Box, early: bool) -> Box {\n  if early { return b }\n  b\n}\n",
    );
    assert_eq!(refused(&fine).len(), 0, "{:?}", refused(&fine));
    let again = program(
        "fn pick(b: take Box, early: bool) -> Box {\n  if early { c = b }\n  b\n}\n",
    );
    assert_eq!(refused(&again).len(), 1, "{:?}", refused(&again));
    // A lent value is not returned, by a `return` as it is not by the last line.
    let lent = program("fn pick(b: Box, early: bool) -> Box {\n  if early { return b }\n  Box(name: \"x\")\n}\n");
    assert_eq!(refused(&lent).len(), 1, "{:?}", refused(&lent));
}

fn every_executable_of_a_program_with_return_does_what_the_evaluator_says() {
    let targets = targets::runnable();
    generate::check(
        66,
        12,
        |g| (items(g, 2, true), g.one_in(2)),
        |(lines, in_lambda)| {
            let (source, said) = if *in_lambda {
                (lambda_program(lines), lambda_printed(lines))
            } else {
                (program(&body(lines, "x")), printed(lines))
            };
            for &target in &targets {
                targets::runs_as(source.as_bytes(), target, &said);
            }
        },
    );
}
