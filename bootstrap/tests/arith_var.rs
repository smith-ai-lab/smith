//! What `arith` knows of a `var` of a whole number, outside a loop as inside
//! one: what it was last given and the condition of each `if` around a use,
//! and nothing from before an assignment that may lie between.
//!
//! Every generator works out what the program prints with the arithmetic of
//! Rust on 128 bits, by running the program's steps for the values of the
//! parameter that reach each branch, so it shares nothing with the compiler.

use smith::scalar::{self, Outcome};
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::numbers::{INTS, Int};

pub const ONCE: &[Test] = &[
    (
        "a_program_that_compiles_never_uses_what_a_var_held_before_an_assignment_that_may_lie_between",
        a_program_that_compiles_never_uses_what_a_var_held_before_an_assignment_that_may_lie_between,
    ),
    (
        "a_var_has_the_condition_around_a_use_and_what_it_was_given_whether_a_loop_is_around_or_not",
        a_var_has_the_condition_around_a_use_and_what_it_was_given_whether_a_loop_is_around_or_not,
    ),
];

/// The values of the parameter `c` that every program is run for: each side
/// of every number a condition on `c` is made from.
const CS: [i128; 10] = [0, 1, 2, 3, 4, 99, 100, 101, 254, 255];

/// The numbers a condition on `c` is made from.
const BOUNDS: [i128; 5] = [0, 2, 3, 100, 255];

/// A variable of the function.
#[derive(Clone, Copy, Debug, PartialEq)]
enum V {
    N,
    M,
}

impl V {
    fn name(self) -> &'static str {
        match self {
            V::N => "n",
            V::M => "m",
        }
    }

    fn at(self) -> usize {
        self as usize
    }
}

/// One step of the function.
#[derive(Clone, Debug)]
enum Step {
    /// `v = a`.
    Set(V, i128),
    /// `if c < k { v = a }`.
    IfSet(V, i128, i128),
    /// `match c { k { v = a }  r { v = b } }`.
    MatchSet(V, i128, i128, i128),
    /// `loop i in 0..times { v = a }`.
    LoopSet(V, usize, i128),
    /// `call() { v = a }`, where `call` calls what it is given twice.
    LambdaSet(V, i128),
    /// `call() { print("{v + k}")  v = a }`.
    LambdaUse(V, i128, i128),
    /// `if c < k { if c < j { v = a } ; use }`, with the use being a print of
    /// `v + 0` after the inner `if`.
    Nested(V, i128, i128, i128),
    /// `v = from + d`, which is a `var` given itself when `from` is `v`.
    Add(V, V, i128),
    /// `print("{from + k}")`.
    Plus(V, i128),
    /// `print("{bit_and(from, mask)}")`, of an unsigned type.
    Bit(V, i128),
    /// `print("{id(from)}")`.
    Id(V),
    /// `if v < limit { print("{v + k}") }`.
    Guard(V, i128, i128),
}

#[derive(Debug)]
struct Program {
    ty: Int,
    start: [i128; 2],
    /// The number of rounds of the `loop` around the steps, or none.
    rounds: Option<usize>,
    steps: Vec<Step>,
}

/// A number of the type that is a limit of it or near one, or small.
fn number(g: &mut Gen, ty: Int) -> i128 {
    let pool = [
        0,
        1,
        2,
        ty.max(),
        ty.max() - 1,
        ty.max() / 2,
        ty.min(),
        ty.min() + 1,
        ty.max() - 100,
        100,
    ];
    (*g.pick(&pool)).clamp(ty.min(), ty.max())
}

/// A number to add, which is not below 0.
fn amount(g: &mut Gen, ty: Int) -> i128 {
    number(g, ty).max(0)
}

/// `n` three times in four, so that what a step gives it is often used.
fn var(g: &mut Gen) -> V {
    *g.pick(&[V::N, V::N, V::N, V::M])
}

fn step(g: &mut Gen, ty: Int) -> Step {
    let kinds = if ty.signed { 11 } else { 12 };
    match g.below(kinds) {
        0 => Step::Set(var(g), number(g, ty)),
        1 => Step::IfSet(var(g), *g.pick(&BOUNDS), number(g, ty)),
        2 => Step::MatchSet(var(g), *g.pick(&BOUNDS), number(g, ty), number(g, ty)),
        3 => Step::LoopSet(var(g), g.below(3), number(g, ty)),
        4 => Step::LambdaSet(var(g), number(g, ty)),
        10 => Step::LambdaUse(var(g), amount(g, ty), number(g, ty)),
        5 => Step::Nested(var(g), 255, *g.pick(&BOUNDS), number(g, ty)),
        6 => Step::Add(var(g), var(g), amount(g, ty)),
        7 => Step::Plus(var(g), amount(g, ty)),
        8 => Step::Id(var(g)),
        9 => Step::Guard(var(g), number(g, ty).max(1), amount(g, ty)),
        _ => Step::Bit(var(g), 1 + g.below(255) as i128),
    }
}

fn program(g: &mut Gen) -> Program {
    let ty = *g.pick(INTS);
    let rounds = [None, Some(1), Some(2)][g.below(3)];
    let count = 1 + g.below(6);
    Program {
        ty,
        start: [*g.pick(&[0, 1, 2, 5]), number(g, ty)],
        rounds,
        steps: (0..count).map(|_| step(g, ty)).collect(),
    }
}

fn source(p: &Program) -> String {
    let mut out = String::from("fn call(f: () -> ()) {\n  f()\n  f()\n}\n\n");
    out.push_str(&format!("fn id(x: {0}) -> {0} {{ x }}\n\n", p.ty.name));
    out.push_str("fn f(c: u8) ! console, alloc {\n");
    for v in [V::N, V::M] {
        out.push_str(&format!(
            "  var {}: {} = {}\n",
            v.name(),
            p.ty.name,
            p.start[v.at()]
        ));
    }
    let mut pad = "  ";
    let looped = p.rounds.is_some();
    if let Some(rounds) = p.rounds {
        out.push_str(&format!("  loop r in 0..{rounds} {{\n"));
        pad = "    ";
    }
    for s in &p.steps {
        let text = match s {
            Step::Set(v, a) => format!("{} = {a}", v.name()),
            Step::IfSet(v, k, a) => format!("if c < {k} {{ {} = {a} }}", v.name()),
            Step::MatchSet(v, k, a, b) => format!(
                "match c {{\n{pad}  {k} {{ {0} = {a} }}\n{pad}  r2 {{ {0} = {b} }}\n{pad}}}",
                v.name()
            ),
            Step::LoopSet(v, times, a) => {
                format!("loop j in 0..{times} {{ {} = {a} }}", v.name())
            }
            Step::LambdaSet(v, a) => format!("call() {{ {} = {a} }}", v.name()),
            Step::LambdaUse(v, k, a) => format!(
                "call() {{\n{pad}  print(\"{{{0} + {k}}}\")\n{pad}  {0} = {a}\n{pad}}}",
                v.name()
            ),
            Step::Nested(v, outer, inner, a) => format!(
                "if c < {outer} {{\n{pad}  if c < {inner} {{ {0} = {a} }}\n{pad}  print(\"{{{0} + 0}}\")\n{pad}}}",
                v.name()
            ),
            Step::Add(v, from, d) => format!("{} = {} + {d}", v.name(), from.name()),
            Step::Plus(from, k) => format!("print(\"{{{} + {k}}}\")", from.name()),
            Step::Bit(from, mask) => format!("print(\"{{bit_and({}, {mask})}}\")", from.name()),
            Step::Id(from) => format!("print(\"{{id({})}}\")", from.name()),
            Step::Guard(v, limit, k) => format!(
                "if {0} < {limit} {{ print(\"{{{0} + {k}}}\") }}",
                v.name()
            ),
        };
        out.push_str(&format!("{pad}{text}\n"));
    }
    if looped {
        out.push_str("  }\n");
    }
    out.push_str("}\n\nfn main() ! console, alloc {\n");
    for c in CS {
        out.push_str(&format!("  f({c})\n"));
    }
    out.push_str("}\n");
    out
}

/// What the program prints for `c`, or none when an addition does not fit.
fn model(p: &Program, c: i128, out: &mut String) -> Option<()> {
    let mut vars = p.start;
    let fit = |n: i128| (p.ty.min()..=p.ty.max()).contains(&n).then_some(n);
    for _ in 0..p.rounds.unwrap_or(1) {
        for s in &p.steps {
            match s {
                Step::Set(v, a) => vars[v.at()] = *a,
                Step::IfSet(v, k, a) => {
                    if c < *k {
                        vars[v.at()] = *a;
                    }
                }
                Step::MatchSet(v, k, a, b) => vars[v.at()] = if c == *k { *a } else { *b },
                Step::LoopSet(v, times, a) => {
                    if *times > 0 {
                        vars[v.at()] = *a;
                    }
                }
                Step::LambdaSet(v, a) => vars[v.at()] = *a,
                Step::LambdaUse(v, k, a) => {
                    for _ in 0..2 {
                        out.push_str(&format!("{}\n", fit(vars[v.at()] + k)?));
                        vars[v.at()] = *a;
                    }
                }
                Step::Nested(v, outer, inner, a) => {
                    if c < *outer {
                        if c < *inner {
                            vars[v.at()] = *a;
                        }
                        out.push_str(&format!("{}\n", fit(vars[v.at()])?));
                    }
                }
                Step::Add(v, from, d) => vars[v.at()] = fit(vars[from.at()] + d)?,
                Step::Plus(from, k) => out.push_str(&format!("{}\n", fit(vars[from.at()] + k)?)),
                Step::Bit(from, mask) => {
                    out.push_str(&format!("{}\n", vars[from.at()] & mask));
                }
                Step::Id(from) => out.push_str(&format!("{}\n", vars[from.at()])),
                Step::Guard(v, limit, k) => {
                    if vars[v.at()] < *limit {
                        out.push_str(&format!("{}\n", fit(vars[v.at()] + k)?));
                    }
                }
            }
        }
    }
    Some(())
}

fn run(source: &str) -> Outcome {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    scalar::run(&program)
}

/// For every function with steps that give a `var` a value in a branch, an
/// arm, a loop that may run no time, an earlier round of a loop around the
/// whole, a lambda that is called, and a block after an inner `if`, or that
/// give it itself or the other `var` plus a number, the program compiles only
/// when no addition overflows for any `c`, and then prints what the steps give.
fn a_program_that_compiles_never_uses_what_a_var_held_before_an_assignment_that_may_lie_between() {
    generate::check(41, 100, program, |p| {
        let text = source(p);
        if check(text.as_bytes()).is_err() {
            return;
        }
        let mut expected = String::new();
        for c in CS {
            model(p, c, &mut expected).unwrap_or_else(|| {
                panic!("compiled, though an addition overflows for c = {c}\n{text}")
            });
        }
        assert_eq!(
            run(&text),
            Outcome {
                stdout: expected.into_bytes(),
                status: 0
            },
            "{text}"
        );
    });
}

#[derive(Debug)]
struct Fact {
    ty: Int,
    /// What `n` is given: a number, or the parameter `x`.
    given: Option<i128>,
    limit: i128,
    k: i128,
    /// A step that gives the other `var` a value, between the fact and the
    /// use.
    between: usize,
}

fn fact(g: &mut Gen) -> Fact {
    let ty = *g.pick(INTS);
    let limit = number(g, ty).max(1);
    let given = g.one_in(2).then(|| g.below(limit.min(4) as usize) as i128);
    Fact {
        ty,
        given,
        limit,
        k: amount(g, ty),
        between: g.below(4),
    }
}

/// `var n` is given a number or a parameter, an `if` says it is below a
/// limit, and `n + k` is in it. It compiles exactly when `n + k` fits for the
/// greatest `n` the facts allow, and the same program with a `loop` around it
/// is accepted and refused as the one without. A step that gives another `var`
/// a value in between changes nothing.
fn a_var_has_the_condition_around_a_use_and_what_it_was_given_whether_a_loop_is_around_or_not() {
    generate::check(42, 40, fact, |f| {
        let greatest = f.given.unwrap_or(f.limit - 1);
        let fits = greatest + f.k <= f.ty.max();
        let build = |looped: bool| {
            let init = f.given.map_or("x".to_string(), |n| n.to_string());
            let between = [
                "",
                "m = 7",
                "if c < 3 { m = 9 }",
                "loop j in 0..2 { m = 4 }",
            ][f.between];
            let (open, close) = if looped {
                ("loop r in 0..1 {\n", "}\n")
            } else {
                ("", "")
            };
            format!(
                "fn f(x: {0}, c: u8) ! console, alloc {{\n  var n: {0} = {init}\n  var m: {0} = 0\n  {open}  {between}\n  if n < {1} {{ print(\"{{n + {2}}}\") }}\n  {close}}}\n\nfn main() ! console, alloc {{\n  f({3}, 1)\n}}\n",
                f.ty.name,
                f.limit,
                f.k,
                f.given.unwrap_or(0)
            )
        };
        for looped in [false, true] {
            let text = build(looped);
            let accepted = check(text.as_bytes()).is_ok();
            assert_eq!(accepted, fits, "a loop around is {looped}\n{text}");
            if accepted && f.given.is_some() {
                assert_eq!(
                    run(&text).stdout,
                    format!("{}\n", greatest + f.k).into_bytes(),
                    "{text}"
                );
            }
        }
        // Whether a loop is around it makes no difference to either side.
        assert_eq!(
            check(build(false).as_bytes()).is_ok(),
            check(build(true).as_bytes()).is_ok()
        );
    });
}
