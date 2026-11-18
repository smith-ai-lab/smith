//! Properties of a `loop` over a value: it runs the `each` of the value's
//! `Iterate` with the block of the loop as the function `each` calls, and a
//! `break` in the block answers `.stop`, as a `return` does after it keeps its
//! value for the function to give.
//!
//! The generator builds a block out of a model of its lines, and works the
//! answer out by running the model in Rust, so it shares nothing with the
//! compiler. Each block is written twice: as a `loop`, and as the call of
//! `each` with a lambda that the page of `Iterate` says the `loop` means.

use smith::diag::Error;
use smith::scalar::{self, Outcome};
use smith::target::Target;
use smith::{check, compile, lower};

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "a_loop_over_a_value_prints_what_the_call_of_each_with_a_lambda_prints",
        a_loop_over_a_value_prints_what_the_call_of_each_with_a_lambda_prints,
    ),
    (
        "a_loop_over_a_list_that_cannot_stop_builds_to_the_bytes_of_the_loop_over_its_places",
        a_loop_over_a_list_that_cannot_stop_builds_to_the_bytes_of_the_loop_over_its_places,
    ),
    (
        "a_loop_over_a_list_that_cannot_stop_builds_and_tests_no_flow",
        a_loop_over_a_list_that_cannot_stop_builds_and_tests_no_flow,
    ),
    (
        "a_loop_over_a_text_gives_its_bytes",
        a_loop_over_a_text_gives_its_bytes,
    ),
    (
        "a_loop_over_a_type_with_no_iterate_is_one_error_that_names_the_type_and_iterate",
        a_loop_over_a_type_with_no_iterate_is_one_error_that_names_the_type_and_iterate,
    ),
    (
        "a_loop_over_a_type_of_the_program_is_checked_as_a_loop_over_a_range_is",
        a_loop_over_a_type_of_the_program_is_checked_as_a_loop_over_a_range_is,
    ),
    (
        "the_examples_of_the_page_that_are_built_give_what_it_says",
        the_examples_of_the_page_that_are_built_give_what_it_says,
    ),
    (
        "a_function_may_be_named_each_and_a_loop_over_a_list_still_runs",
        a_function_may_be_named_each_and_a_loop_over_a_list_still_runs,
    ),
];

/// A test of the element or of `acc`.
#[derive(Clone, Debug)]
struct Cond {
    on_acc: bool,
    /// 0 for `<`, 1 for `>`, 2 for `==`.
    op: usize,
    k: i64,
}

/// What a `return` gives.
#[derive(Clone, Debug)]
enum Val {
    Acc,
    Element,
    Lit(i64),
}

/// A line of the block of a loop whose element is `v`, in a function that
/// has `var acc`.
#[derive(Clone, Debug)]
enum Item {
    /// `acc = wrapping_add(acc, wrapping_add(v, k))`.
    Bump(i64),
    /// `if c { break }`.
    Break(Cond),
    /// `if c { return e }`.
    Return(Cond, Val),
    Branch(Cond, Vec<Item>, Vec<Item>),
    /// `loop j in 0..2 { ... }`, whose `break` ends it and no other.
    Inner(Vec<Item>),
}

/// What is walked.
#[derive(Clone, Debug)]
enum Walked {
    /// A list of numbers.
    List(Vec<i64>),
    /// A `Down`, which gives `from`, then one less, down to 1.
    Down(i64),
}

impl Walked {
    fn elements(&self) -> Vec<i64> {
        match self {
            Walked::List(xs) => xs.clone(),
            Walked::Down(n) => (1..=*n).rev().collect(),
        }
    }

    fn type_name(&self) -> &'static str {
        match self {
            Walked::List(_) => "List[i64]",
            Walked::Down(_) => "Down",
        }
    }

    fn written(&self) -> String {
        match self {
            Walked::List(xs) => {
                let items: Vec<String> = xs.iter().map(i64::to_string).collect();
                format!("[{}]", items.join(", "))
            }
            Walked::Down(n) => format!("Down(from: {n})"),
        }
    }
}

impl Cond {
    fn holds(&self, v: i64, acc: i64) -> bool {
        let x = if self.on_acc { acc } else { v };
        match self.op {
            0 => x < self.k,
            1 => x > self.k,
            _ => x == self.k,
        }
    }

    fn write(&self) -> String {
        let x = if self.on_acc { "acc" } else { "v" };
        let op = ["<", ">", "=="][self.op];
        format!("{x} {op} {}", self.k)
    }
}

impl Val {
    fn eval(&self, v: i64, acc: i64) -> i64 {
        match self {
            Val::Acc => acc,
            Val::Element => v,
            Val::Lit(k) => *k,
        }
    }

    fn write(&self) -> String {
        match self {
            Val::Acc => "acc".into(),
            Val::Element => "v".into(),
            Val::Lit(k) => k.to_string(),
        }
    }
}

fn cond(g: &mut Gen) -> Cond {
    Cond {
        on_acc: g.one_in(2),
        op: g.below(3),
        k: g.below(9) as i64 - 2,
    }
}

fn items(g: &mut Gen, depth: usize, stops: bool, inner: bool) -> Vec<Item> {
    let count = g.below(4);
    (0..count)
        .map(|_| match g.below(if stops { 5 } else { 3 }) {
            0 => Item::Bump(g.below(5) as i64),
            1 if depth > 0 => Item::Branch(
                cond(g),
                items(g, depth - 1, stops, inner),
                items(g, depth - 1, stops, inner),
            ),
            2 if depth > 0 && !inner => Item::Inner(items(g, depth - 1, stops, true)),
            3 => Item::Break(cond(g)),
            4 => Item::Return(
                cond(g),
                match g.below(3) {
                    0 => Val::Acc,
                    1 => Val::Element,
                    _ => Val::Lit(g.below(20) as i64 + 100),
                },
            ),
            _ => Item::Bump(1),
        })
        .collect()
}

/// How a block ends: it goes on, a `break` ended the loop around the line,
/// or a `return` gave the function a value.
#[derive(PartialEq)]
enum Ended {
    On,
    Break,
    Gave(i64),
}

fn run(lines: &[Item], v: i64, acc: &mut i64) -> Ended {
    for line in lines {
        match line {
            Item::Bump(k) => *acc = acc.wrapping_add(v.wrapping_add(*k)),
            Item::Break(c) => {
                if c.holds(v, *acc) {
                    return Ended::Break;
                }
            }
            Item::Return(c, val) => {
                if c.holds(v, *acc) {
                    return Ended::Gave(val.eval(v, *acc));
                }
            }
            Item::Branch(c, a, b) => {
                let ended = run(if c.holds(v, *acc) { a } else { b }, v, acc);
                if ended != Ended::On {
                    return ended;
                }
            }
            Item::Inner(body) => {
                for _ in 0..2 {
                    match run(body, v, acc) {
                        Ended::On => {}
                        Ended::Break => break,
                        gave => return gave,
                    }
                }
            }
        }
    }
    Ended::On
}

/// What `f` gives for the walked value: what its `return` gave, or `acc`
/// after the loop, which the function adds 1000 to.
fn gives(lines: &[Item], walked: &Walked) -> i64 {
    let mut acc = 0;
    for v in walked.elements() {
        match run(lines, v, &mut acc) {
            Ended::On => {}
            Ended::Break => break,
            Ended::Gave(x) => return x,
        }
    }
    acc.wrapping_add(1000)
}

/// How a block is written.
#[derive(Clone, Copy, PartialEq)]
enum Form {
    /// As the block of a `loop`.
    Loop,
    /// As the lambda of a call of `each`, where a `break` is `.stop`, and a
    /// `return e` keeps `e` and is `.stop`.
    Each,
}

fn write(lines: &[Item], form: Form, inner: bool, out: &mut String) {
    for line in lines {
        match line {
            Item::Bump(k) => out.push_str(&format!("acc = wrapping_add(acc, wrapping_add(v, {k}))\n")),
            Item::Break(c) => {
                let leave = if form == Form::Each && !inner { "return .stop" } else { "break" };
                out.push_str(&format!("if {} {{ {leave} }}\n", c.write()));
            }
            Item::Return(c, val) => match form {
                Form::Loop => out.push_str(&format!("if {} {{ return {} }}\n", c.write(), val.write())),
                Form::Each => out.push_str(&format!(
                    "if {} {{\nkept = {}\nleft = true\nreturn .stop\n}}\n",
                    c.write(),
                    val.write()
                )),
            },
            Item::Branch(c, a, b) => {
                out.push_str(&format!("if {} {{\n", c.write()));
                write(a, form, inner, out);
                out.push_str("} else {\n");
                write(b, form, inner, out);
                out.push_str("}\n");
            }
            Item::Inner(body) => {
                out.push_str("loop j in 0..2 {\n");
                write(body, form, true, out);
                out.push_str("}\n");
            }
        }
    }
}

const DOWN: &str = "struct Down {
  from: i64
}

impl Iterate[Down, i64] {
  fn each(c: Down, body: (i64) -> Flow) -> Flow {
    loop i in 0..c.from {
      match body(c.from - i) {
        .stop { return .stop }
        .go { }
      }
    }
    .go
  }
}

";

/// A program whose `f` walks the value with the lines, written as `form`.
fn program(lines: &[Item], walked: &Walked, form: Form) -> String {
    let mut block = String::new();
    write(lines, form, false, &mut block);
    let function = match form {
        Form::Loop => format!(
            "fn f(c: {}) -> i64 {{\nvar acc: i64 = 0\nloop v in c {{\n{block}}}\nwrapping_add(acc, 1000)\n}}\n",
            walked.type_name()
        ),
        Form::Each => format!(
            "fn f(c: {}) -> i64 {{\nvar acc: i64 = 0\nvar kept: i64 = 0\nvar left = false\neach(c) fn(v) {{\n{block}.go\n}}\nif left {{ return kept }}\nwrapping_add(acc, 1000)\n}}\n",
            walked.type_name()
        ),
    };
    let setup = match walked {
        Walked::List(_) => "",
        Walked::Down(_) => DOWN,
    };
    format!(
        "{setup}{function}\nfn main() ! console, alloc {{\n  c: {} = {}\n  print(\"{{f(c)}}\")\n}}\n",
        walked.type_name(),
        walked.written()
    )
}

fn says(text: &str) -> Outcome {
    Outcome {
        stdout: format!("{text}\n").into_bytes(),
        status: 0,
    }
}

fn evaluate(source: &str) -> Outcome {
    let lowered = std::panic::catch_unwind(|| lower(source.as_bytes()));
    let program = lowered
        .unwrap_or_else(|_| panic!("the compiler panics for {source}"))
        .unwrap_or_else(|e| panic!("{e:?} for {source}"));
    assert_eq!(scalar::check(&program), Ok(()), "for {source}");
    assert_eq!(scalar::check(&scalar::passes(&program)), Ok(()), "for {source}");
    scalar::run(&program)
}

fn walked(g: &mut Gen) -> Walked {
    if g.one_in(2) {
        Walked::Down(g.below(5) as i64)
    } else {
        let count = g.below(5);
        Walked::List((0..count).map(|_| g.below(8) as i64 - 1).collect())
    }
}

fn a_loop_over_a_value_prints_what_the_call_of_each_with_a_lambda_prints() {
    generate::check(
        705,
        24,
        |g| (items(g, 2, true, false), walked(g)),
        |(lines, walked)| {
            let said = says(&gives(lines, walked).to_string());
            let looped = program(lines, walked, Form::Loop);
            assert_eq!(evaluate(&looped), said, "for {looped}");
            let written = program(lines, walked, Form::Each);
            assert_eq!(evaluate(&written), said, "for {written}");
        },
    );
}

/// The program of the lines over the list `xs`, with the loop written as
/// `loop`, or as a loop over the places of the list.
fn over_places(lines: &[Item], places: bool) -> String {
    let mut block = String::new();
    write(lines, Form::Loop, false, &mut block);
    let head = if places {
        "loop at in 0..xs.size {\nv = xs[at]\n"
    } else {
        "loop v in xs {\n"
    };
    format!(
        "fn f(xs: List[i64]) -> i64 {{\nvar acc: i64 = 0\n{head}{block}}}\nacc\n}}\n\nfn main() ! console, alloc {{\n  xs: List[i64] = [1, 2, 3]\n  print(\"{{f(xs)}}\")\n}}\n"
    )
}

fn a_loop_over_a_list_that_cannot_stop_builds_to_the_bytes_of_the_loop_over_its_places() {
    generate::check(
        706,
        6,
        |g| items(g, 2, false, false),
        |lines| {
            let looped = over_places(lines, false);
            let written = over_places(lines, true);
            // The executables of the targets differ in how they place what the
            // scalar form says, which this does not change, so one is enough.
            let target = Target::ALL[0];
            let a = compile(looped.as_bytes(), target).unwrap_or_else(|e| panic!("{e:?} for {looped}"));
            let b = compile(written.as_bytes(), target).unwrap_or_else(|e| panic!("{e:?} for {written}"));
            assert!(a == b, "the executables differ for {looped}\nand {written}");
        },
    );
}

fn a_loop_over_a_list_that_cannot_stop_builds_and_tests_no_flow() {
    generate::check(
        707,
        6,
        |g| items(g, 2, false, false),
        |lines| {
            let looped = over_places(lines, false);
            let written = over_places(lines, true);
            let size = |source: &str| {
                let program = scalar::passes(&lower(source.as_bytes()).expect("compiles"));
                program.functions.iter().map(|f| f.blocks.len()).sum::<usize>()
            };
            assert_eq!(size(&looped), size(&written), "for {looped}");
        },
    );
}

fn a_loop_over_a_text_gives_its_bytes() {
    const PIECES: [&str; 6] = ["a", "hi", "é", "日本", "x y", ""];
    generate::check(
        708,
        10,
        |g| {
            let count = g.below(4);
            (0..count).map(|_| PIECES[g.below(PIECES.len())]).collect::<String>()
        },
        |text| {
            let source = format!(
                "fn main() ! console, alloc {{\n  loop b in \"{text}\" {{ print(\"{{b}}\") }}\n  s = \"{text}\"\n  loop b in s {{ print(\"{{b}}\") }}\n}}\n"
            );
            let bytes: String = text.bytes().map(|b| format!("{b}\n")).collect();
            let expected = Outcome {
                stdout: format!("{bytes}{bytes}").into_bytes(),
                status: 0,
            };
            assert_eq!(evaluate(&source), expected, "for {source}");
        },
    );
}

fn errors(source: &str) -> Vec<Error> {
    check(source.as_bytes()).err().unwrap_or_default()
}

fn a_loop_over_a_type_with_no_iterate_is_one_error_that_names_the_type_and_iterate() {
    const NAMES: [&str; 3] = ["Cat", "Mug", "Box"];
    generate::check(
        709,
        6,
        |g| (NAMES[g.below(NAMES.len())], g.below(2) == 0),
        |(name, an_enum)| {
            let definition = if *an_enum {
                format!("enum {name} {{\n  one\n  two\n}}\n\nfn make() -> {name} {{ .one }}\n")
            } else {
                format!("struct {name} {{\n  n: u8\n}}\n\nfn make() -> {name} {{ {name}(n: 1) }}\n")
            };
            let source = format!(
                "{definition}\nfn main() ! console, alloc {{\n  c = make()\n  loop v in c {{ print(\"x\") }}\n}}\n"
            );
            let found = errors(&source);
            assert_eq!(found.len(), 1, "{found:?} for {source}");
            assert!(found[0].message.contains(name), "{found:?}");
            assert!(found[0].message.contains("Iterate"), "{found:?}");
        },
    );
}

/// The program where the block of a loop gives away a value it did not
/// define, or one that it did, where the loop is over `head`.
fn gives_away(head: &str, defined_inside: bool) -> String {
    let (outside, inside) = if defined_inside {
        ("", "own = Key(secret: 1)\n    print(\"{spend(own)}\")")
    } else {
        ("  k = Key(secret: 1)\n", "print(\"{spend(k)}\")")
    };
    format!(
        "struct Key {{\n  secret: u64\n}}\n\nwithout Copy for Key\n\n{DOWN}fn spend(k: take Key) -> u64 {{ k.secret }}\n\nfn main() ! console, alloc {{\n{outside}  xs: List[i64] = [1, 2]\n  loop v in {head} {{\n    {inside}\n  }}\n}}\n"
    )
}

fn a_loop_over_a_type_of_the_program_is_checked_as_a_loop_over_a_range_is() {
    for defined_inside in [false, true] {
        let messages = |head: &str| -> Vec<String> {
            errors(&gives_away(head, defined_inside))
                .into_iter()
                .map(|e| e.message)
                .collect()
        };
        let range = messages("0..2");
        assert_eq!(messages("Down(from: 2)"), range);
        assert_eq!(messages("xs"), range);
        assert_eq!(range.is_empty(), defined_inside, "{range:?}");
    }
}

fn the_examples_of_the_page_that_are_built_give_what_it_says() {
    let countdown = "struct Countdown {
  from: i32
}

impl Iterate[Countdown, i32] {
  fn each(c: Countdown, body: (i32) -> Flow) -> Flow {
    loop i in 0..c.from {
      match body(c.from - i) {
        .stop { return .stop }
        .go { }
      }
    }
    .go
  }
}

";
    let each = format!(
        "{countdown}fn main() ! console, alloc {{\n  each(Countdown(from: 3)) fn(v) {{\n    print(\"{{v}}\")\n    if v == 2 {{ .stop }} else {{ .go }}\n  }}\n  print(\"done\")\n}}\n"
    );
    assert_eq!(evaluate(&each), Outcome { stdout: b"3\n2\ndone\n".to_vec(), status: 0 });
    let largest = format!(
        "{countdown}fn largest(c: Countdown) -> i32 {{\n  var top = 0\n  loop v in c {{ if v > top {{ top = v }} }}\n  top\n}}\n\nfn first_small(c: Countdown) -> i32 {{\n  var found = 0\n  loop v in c {{\n    if v < 3 {{\n      found = v\n      break\n    }}\n  }}\n  found\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{largest(Countdown(from: 3))}}\")\n  print(\"{{first_small(Countdown(from: 5))}}\")\n}}\n"
    );
    assert_eq!(evaluate(&largest), Outcome { stdout: b"3\n2\n".to_vec(), status: 0 });
    // The block of a loop is checked where it is written, against the effects
    // of the function that has the loop.
    let show = format!(
        "{countdown}fn show(c: Countdown) ! alloc {{\n  loop v in c {{ print(\"{{v}}\") }}\n}}\n\nfn main() ! console, alloc {{\n  show(Countdown(from: 2))\n}}\n"
    );
    let found = errors(&show);
    assert!(found.iter().any(|e| e.message.contains("console")), "{found:?}");
}

fn a_function_may_be_named_each_and_a_loop_over_a_list_still_runs() {
    let source = "fn each(f: (str) -> str) -> str { f(\"q\") }\n\nfn main() ! console, alloc {\n  print(each() fn(v) { v })\n  xs: List[u8] = [7, 8]\n  loop x in xs { print(\"{x}\") }\n}\n";
    assert_eq!(evaluate(source), Outcome { stdout: b"q\n7\n8\n".to_vec(), status: 0 });
}
