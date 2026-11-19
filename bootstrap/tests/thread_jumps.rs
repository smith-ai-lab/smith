//! A case that is built and matched at once is never made: a jump into a block
//! that only chooses goes to the block the choice takes, so
//! the code of the arm that builds a case goes to the code of the arm that
//! matches it, and the number of the case is neither made nor read.
//!
//! The structure is counted against the same program with the arms written
//! directly, so what the program prints costs the same in both. The answers
//! are what Rust says, for values the compiler cannot know because they come
//! out of a list.

use smith::lower;
use smith::scalar::{self, End, Op};

use crate::Test;
use crate::generate::{self, Gen};
use crate::numbers::with_header;
use crate::targets::{runnable, runs_as};

pub const ONCE: &[Test] = &[
    (
        "a_case_built_in_each_arm_of_an_if_and_matched_at_once_costs_what_the_arms_written_directly_cost",
        || costs_what_the_arms_cost(Context::If),
    ),
    (
        "a_case_built_in_each_arm_of_a_match_and_matched_at_once_costs_what_the_arms_written_directly_cost",
        || costs_what_the_arms_cost(Context::Match),
    ),
    (
        "a_chain_of_three_blocks_that_each_build_a_case_and_match_one_is_one_choice",
        a_chain_of_three_is_one_choice,
    ),
    (
        "a_case_that_is_built_and_matched_at_once_gives_what_the_arm_it_takes_gives",
        || the_evaluator_answers(&kinds_program),
    ),
    (
        "a_parameter_given_a_constant_on_one_jump_and_a_value_that_is_not_known_on_another_is_still_chosen_on",
        || the_evaluator_answers(&unknown_program),
    ),
    (
        "a_block_jumped_to_from_the_end_of_a_loop_goes_where_its_choice_goes",
        || the_evaluator_answers(&loop_program),
    ),
    (
        "a_value_that_is_read_after_the_match_is_still_made",
        || the_evaluator_answers(&read_again_program),
    ),
    (
        "a_block_that_writes_memory_or_calls_before_its_choice_does_so_for_every_jump_into_it",
        || the_evaluator_answers(&effects_program),
    ),
    (
        "a_chain_of_three_blocks_that_only_choose_gives_what_the_arm_it_ends_in_gives",
        || the_evaluator_answers(&chain_program),
    ),
];

pub const MACHINE: &[Test] = &[
    (
        "every_executable_answers_for_a_case_that_is_built_and_matched_at_once",
        || on_every_executable(&kinds_program),
    ),
    (
        "every_executable_answers_for_a_parameter_given_a_constant_and_a_value_that_is_not_known",
        || on_every_executable(&unknown_program),
    ),
    (
        "every_executable_answers_for_a_block_jumped_to_from_the_end_of_a_loop",
        || on_every_executable(&loop_program),
    ),
    (
        "every_executable_answers_for_a_value_that_is_read_after_the_match",
        || on_every_executable(&read_again_program),
    ),
    (
        "every_executable_answers_for_a_block_that_writes_memory_or_calls_before_its_choice",
        || on_every_executable(&effects_program),
    ),
    (
        "every_executable_answers_for_a_chain_of_three_blocks_that_only_choose",
        || on_every_executable(&chain_program),
    ),
];

/// A program, and what it prints.
type Program = (String, String);

/// Where the case is built: in the two arms of an `if`, or in the arms of a
/// `match` on a number.
#[derive(Clone, Copy)]
enum Context {
    If,
    Match,
}

/// A case of an enum: how it is built from the number `n`, how it is
/// matched, what its arm gives (with `@` for what the case holds), and what
/// that is for a number.
struct Case {
    make: &'static str,
    pattern: &'static str,
    arm: &'static str,
    gives: fn(u64) -> u64,
}

struct Kind {
    /// The declaration the program needs, if the enum is not in the prelude.
    declaration: &'static str,
    ty: &'static str,
    cases: &'static [Case],
}

const KINDS: &[Kind] = &[
    Kind {
        declaration: "",
        ty: "Option[u64]",
        cases: &[
            Case {
                make: ".some(n)",
                pattern: ".some(v)",
                arm: "wrapping_add(@, 100)",
                gives: |n| n.wrapping_add(100),
            },
            Case {
                make: ".none",
                pattern: ".none",
                arm: "7",
                gives: |_| 7,
            },
        ],
    },
    Kind {
        declaration: "",
        ty: "Result[u64, u64]",
        cases: &[
            Case {
                make: ".ok(n)",
                pattern: ".ok(v)",
                arm: "wrapping_add(@, 100)",
                gives: |n| n.wrapping_add(100),
            },
            Case {
                make: ".err(n)",
                pattern: ".err(v)",
                arm: "wrapping_add(@, 200)",
                gives: |n| n.wrapping_add(200),
            },
        ],
    },
    Kind {
        declaration: "",
        ty: "bool",
        cases: &[
            Case {
                make: "true",
                pattern: "true",
                arm: "1",
                gives: |_| 1,
            },
            Case {
                make: "false",
                pattern: "false",
                arm: "2",
                gives: |_| 2,
            },
        ],
    },
    Kind {
        declaration: "",
        ty: "Flow",
        cases: &[
            Case {
                make: ".go",
                pattern: ".go",
                arm: "3",
                gives: |_| 3,
            },
            Case {
                make: ".stop",
                pattern: ".stop",
                arm: "4",
                gives: |_| 4,
            },
        ],
    },
    Kind {
        declaration: "enum Five {\n  a: u64\n  b\n  c: u64\n  d\n  e\n}\n",
        ty: "Five",
        cases: &[
            Case {
                make: ".a(n)",
                pattern: ".a(v)",
                arm: "@",
                gives: |n| n,
            },
            Case {
                make: ".b",
                pattern: ".b",
                arm: "11",
                gives: |_| 11,
            },
            Case {
                make: ".c(n)",
                pattern: ".c(v)",
                arm: "wrapping_add(@, 22)",
                gives: |n| n.wrapping_add(22),
            },
            Case {
                make: ".d",
                pattern: ".d",
                arm: "33",
                gives: |_| 33,
            },
            Case {
                make: ".e",
                pattern: ".e",
                arm: "44",
                gives: |_| 44,
            },
        ],
    },
];

/// The case that the number `n` builds: in an `if`, the first when `n` is
/// below 3 and the last when it is not, and in a `match`, the one at the place
/// `n` when `n` is below the last, and the last when it is not.
fn built(kind: &Kind, context: Context, n: u64) -> &Case {
    let last = kind.cases.len() - 1;
    let at = match context {
        Context::If if n < 3 => 0,
        Context::If => last,
        Context::Match => (n as usize).min(last),
    };
    &kind.cases[at]
}

/// The cases a context builds, with the text that says when.
fn built_in(kind: &Kind, context: Context) -> Vec<(String, &Case)> {
    let last = kind.cases.len() - 1;
    match context {
        Context::If => vec![
            ("n < 3".to_string(), &kind.cases[0]),
            (String::new(), &kind.cases[last]),
        ],
        Context::Match => (0..=last)
            .map(|k| {
                let when = if k < last { k.to_string() } else { "other".to_string() };
                (when, &kind.cases[k])
            })
            .collect(),
    }
}

/// A function `name` of a number that gives a number, in which the case is
/// built and then matched, or in which each arm gives what the arm that
/// matches would give.
fn function(name: &str, kind: &Kind, context: Context, with_case: bool) -> String {
    let arms = built_in(kind, context);
    let mut out = format!("fn {name}(n: u64) -> u64 {{\n");
    let (open, close) = match context {
        Context::If => ("", ""),
        Context::Match => ("match n {\n", "  }\n"),
    };
    let say = |case: &Case, with_case: bool| {
        if with_case {
            case.make.to_string()
        } else {
            case.arm.replace('@', "n")
        }
    };
    let mut choice = String::new();
    match context {
        Context::If => {
            choice += &format!(
                "if {} {{ {} }} else {{ {} }}",
                arms[0].0,
                say(arms[0].1, with_case),
                say(arms[1].1, with_case)
            );
        }
        Context::Match => {
            choice += open;
            for (when, case) in &arms {
                choice += &format!("    {when} {{ {} }}\n", say(case, with_case));
            }
            choice += close;
            choice = choice.trim_end().to_string();
        }
    }
    if with_case {
        out += &format!("  e: {} = {choice}\n  match e {{\n", kind.ty);
        for case in kind.cases {
            let at = case.arm.replace('@', "v");
            out += &format!("    {} {{ {at} }}\n", case.pattern);
        }
        out += "  }\n";
    } else {
        out += &format!("  {choice}\n");
    }
    out += "}\n";
    out
}

/// A program whose `main` prints what each function gives for each number
/// in the list.
fn with_main(declarations: &str, functions: &str, names: &[&str], xs: &[u64], effects: &str) -> String {
    let list: Vec<String> = xs.iter().map(|x| x.to_string()).collect();
    let mut source = format!("{declarations}{functions}");
    source += &format!("fn main() ! {effects} {{\n  xs: List[u64] = [{}]\n  loop i in 0..xs.size {{\n", list.join(", "));
    for name in names {
        source += &format!("    print(\"{{{name}(xs[i])}}\")\n");
    }
    source += "  }\n}\n";
    with_header(source)
}

/// The numbers a program is run on: the ones next to where an `if` and a
/// `match` change, and a few that the seed picks.
fn numbers(g: &mut Gen) -> Vec<u64> {
    let mut xs: Vec<u64> = (0..8).collect();
    for _ in 0..3 {
        xs.push(8 + g.below(60) as u64);
    }
    xs
}

fn passed(source: &str) -> scalar::Program {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let program = scalar::passes(&program);
    scalar::check(&program).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    program
}

/// How many choices, comparisons, and instructions a program has after the
/// passes.
fn shape(source: &str) -> (usize, usize, usize) {
    let program = passed(source);
    let (mut choices, mut compares, mut instructions) = (0, 0, 0);
    for block in program.functions.iter().flat_map(|f| &f.blocks) {
        if matches!(block.end, End::Switch { .. }) {
            choices += 1;
        }
        for i in &block.instructions {
            instructions += 1;
            if matches!(i.op, Op::Compare { .. }) {
                compares += 1;
            }
        }
    }
    (choices, compares, instructions)
}

/// For each kind of enum, a program that builds the case in each arm and
/// matches it has the choices, comparisons, and instructions of the program
/// in which each arm gives what the arm that matches it gives.
fn costs_what_the_arms_cost(context: Context) {
    for kind in KINDS {
        let build = |with_case| {
            let source = with_main(
                kind.declaration,
                &function("f", kind, context, with_case),
                &["f"],
                &[0, 1, 2, 3, 4, 5, 6, 7, 8],
                "console, alloc",
            );
            (shape(&source), source)
        };
        let (with, source) = build(true);
        let (without, _) = build(false);
        assert_eq!(with, without, "for {source}");
    }
}

fn expected_lines(xs: &[u64], lines: impl Fn(u64) -> Vec<String>) -> String {
    xs.iter().flat_map(|&x| lines(x)).map(|l| l + "\n").collect()
}

/// A program with a function for each kind and each context.
fn kinds_program(g: &mut Gen) -> Program {
    let xs = numbers(g);
    let declarations = KINDS[4].declaration;
    let mut functions = String::new();
    let mut names = Vec::new();
    for (k, kind) in KINDS.iter().enumerate() {
        for (c, context) in [Context::If, Context::Match].into_iter().enumerate() {
            let name = format!("f{k}_{c}");
            functions += &function(&name, kind, context, true);
            names.push((name, kind, context));
        }
    }
    let called: Vec<&str> = names.iter().map(|(n, ..)| n.as_str()).collect();
    let source = with_main(declarations, &functions, &called, &xs, "console, alloc");
    let said = expected_lines(&xs, |x| {
        names
            .iter()
            .map(|(_, kind, context)| (built(kind, *context, x).gives)(x).to_string())
            .collect()
    });
    (source, said)
}

/// A parameter that one jump gives a constant and another gives a number the
/// program does not know, which the block then chooses on.
fn unknown_program(g: &mut Gen) -> Program {
    let xs = numbers(g);
    let functions = "\
fn by_number(n: u64) -> u64 {
  k: u64 = if n < 3 { 1 } else { wrapping_sub(n, 3) }
  match k {
    0 { 10 }
    1 { 20 }
    2 { 30 }
    other { wrapping_add(other, 40) }
  }
}

fn by_flag(n: u64) -> u64 {
  b: bool = if n < 3 { true } else { n == 5 }
  if b { 1 } else { 2 }
}

fn by_either(n: u64) -> u64 {
  b: bool = if n < 3 { n == 1 } else { n == 5 }
  if b { 1 } else { 2 }
}
";
    let source = with_main("", functions, &["by_number", "by_flag", "by_either"], &xs, "console, alloc");
    let said = expected_lines(&xs, |x| {
        let k = if x < 3 { 1 } else { x - 3 };
        let number = match k {
            0 => 10,
            1 => 20,
            2 => 30,
            other => other + 40,
        };
        let flag = if x < 3 || x == 5 { 1 } else { 2 };
        let either = if (x < 3 && x == 1) || (x >= 3 && x == 5) { 1 } else { 2 };
        vec![number.to_string(), flag.to_string(), either.to_string()]
    });
    (source, said)
}

/// A loop that is a machine with three states, which every state ends by
/// giving the state the next time round, so the block that chooses on the
/// state is jumped to from the end of the loop.
fn loop_program(g: &mut Gen) -> Program {
    let xs = numbers(g);
    let functions = "\
enum Phase {
  up
  down
  stop
}

fn run(n: u64) -> u64 ! diverge {
  var phase: Phase = .up
  var total: u64 = 0
  var rounds: u64 = 0
  loop {
    match phase {
      .up {
        total = wrapping_add(total, 3)
        phase = .down
      }
      .down {
        total = wrapping_mul(total, 2)
        phase = .up
      }
      .stop { break }
    }
    rounds = wrapping_add(rounds, 1)
    if rounds >= n { phase = .stop }
  }
  total
}
";
    let source = with_main("", functions, &["run"], &xs, "console, alloc, diverge");
    let said = expected_lines(&xs, |x| {
        let (mut up, mut total, mut rounds) = (true, 0u64, 0u64);
        // `n` rounds, but at least one, as the test is made after the first.
        while rounds < x.max(1) {
            if up {
                total = total.wrapping_add(3);
            } else {
                total = total.wrapping_mul(2);
            }
            up = !up;
            rounds += 1;
        }
        vec![total.to_string()]
    });
    (source, said)
}

/// A case that is built, matched, and then read again, which is still made.
fn read_again_program(g: &mut Gen) -> Program {
    let xs = numbers(g);
    let declarations = "enum Shape {\n  circle: u64\n  square: u64\n  none\n}\n";
    let functions = "\
fn describe(n: u64) -> u64 ! console, alloc {
  s: Shape = if n < 3 { .circle(n) } else { .none }
  r: u64 = match s {
    .circle(v) { wrapping_add(v, 1) }
    .square(v) { v }
    .none { 7 }
  }
  print(\"{s}\")
  r
}
";
    let source = with_main(declarations, functions, &["describe"], &xs, "console, alloc");
    let said = expected_lines(&xs, |x| {
        if x < 3 {
            vec![format!(".circle({x})"), (x + 1).to_string()]
        } else {
            vec![".none".to_string(), "7".to_string()]
        }
    });
    (source, said)
}

/// A block that writes memory and calls before it chooses on a case that
/// the jumps into it have built.
fn effects_program(g: &mut Gen) -> Program {
    let xs = numbers(g);
    let functions = "\
fn shown(e: Option[u64]) -> u64 ! console, alloc {
  print(\"seen\")
  match e {
    .some(v) { v }
    .none { 7 }
  }
}

fn once(n: u64) -> u64 ! console, alloc {
  shown(if n < 3 { .some(n) } else { .none })
}

fn pushed(n: u64) -> u64 ! alloc {
  var l: List[u64] = [0]
  e: Option[u64] = if n < 3 { .some(n) } else { .none }
  l = push(l, wrapping_add(n, 5))
  r: u64 = match e {
    .some(v) { v }
    .none { 7 }
  }
  last: u64 = if 1 < l.size { l[1] } else { 0 }
  wrapping_add(wrapping_mul(r, 1000), last)
}
";
    let source = with_main("", functions, &["once", "pushed"], &xs, "console, alloc");
    let said = expected_lines(&xs, |x| {
        let r = if x < 3 { x } else { 7 };
        vec!["seen".to_string(), r.to_string(), (r * 1000 + x + 5).to_string()]
    });
    (source, said)
}

/// Three blocks that each match the case the one before built.
fn chain_functions() -> &'static str {
    "\
fn chain(n: u64) -> u64 {
  a: Option[u64] = if n < 3 { .some(n) } else { .none }
  b: Result[u64, u64] = match a {
    .some(v) { .ok(wrapping_add(v, 1)) }
    .none { .err(5) }
  }
  c: Flow = match b {
    .ok(v) { .go }
    .err(v) { .stop }
  }
  match c {
    .go { 1 }
    .stop { 2 }
  }
}
"
}

fn chain_program(g: &mut Gen) -> Program {
    let xs = numbers(g);
    let source = with_main("", chain_functions(), &["chain"], &xs, "console, alloc");
    let said = expected_lines(&xs, |x| vec![if x < 3 { "1" } else { "2" }.to_string()]);
    (source, said)
}

/// The choices that three blocks, each matching the case the one before
/// built, leave are the one that the `if` makes.
fn a_chain_of_three_is_one_choice() {
    let by_if = "fn chain(n: u64) -> u64 {\n  if n < 3 { 1 } else { 2 }\n}\n";
    let xs = [0, 1, 2, 3, 4, 5, 6, 7, 8];
    let with = with_main("", chain_functions(), &["chain"], &xs, "console, alloc");
    let without = with_main("", by_if, &["chain"], &xs, "console, alloc");
    assert_eq!(shape(&with), shape(&without), "for {with}");
}

fn the_evaluator_answers(make: &dyn Fn(&mut Gen) -> Program) {
    let (source, said) = generate::sample(0, 0, make);
    let outcome = scalar::run(&passed(&source));
    assert_eq!(String::from_utf8_lossy(&outcome.stdout), said, "for {source}");
    assert_eq!(outcome.status, 0, "for {source}");
}

fn on_every_executable(make: &dyn Fn(&mut Gen) -> Program) {
    let (source, said) = generate::sample(0, 0, make);
    let outcome = scalar::Outcome {
        stdout: said.into_bytes(),
        status: 0,
    };
    for target in runnable() {
        runs_as(source.as_bytes(), target, &outcome);
    }
}
