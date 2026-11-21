//! The kernel works a core term out one step at a time. What it gives of a
//! function is what the program prints, a run that goes past its steps fails
//! and names the limit, and a comparison that gave `false` is reported with
//! where it is written and the numbers it compared.

use smith::core_terms;
use smith::kernel::{Failure, Machine, Value, Why};
use smith::lower;
use smith::scalar::{self, Outcome};

use crate::Test;
use crate::core_producer::program;
use crate::generate::{self, Gen};
use crate::numbers::with_header;

pub const ONCE: &[Test] = &[
    (
        "the_kernel_works_a_function_out_to_what_the_program_prints",
        the_kernel_works_a_function_out_to_what_the_program_prints,
    ),
    (
        "the_kernel_works_a_loop_out_round_by_round_to_what_the_program_prints",
        the_kernel_works_a_loop_out_round_by_round_to_what_the_program_prints,
    ),
    (
        "a_requires_over_arguments_written_out_is_proven_by_working_it_out",
        a_requires_over_arguments_written_out_is_proven_by_working_it_out,
    ),
    (
        "a_predicate_that_does_not_end_within_the_steps_fails_naming_the_call_and_the_limit",
        a_predicate_that_does_not_end_within_the_steps_fails_naming_the_call_and_the_limit,
    ),
    (
        "a_run_past_its_steps_fails_and_the_steps_grow_with_what_it_works_out",
        a_run_past_its_steps_fails_and_the_steps_grow_with_what_it_works_out,
    ),
    (
        "a_false_comparison_is_reported_where_it_is_written_with_the_numbers_it_compared",
        a_false_comparison_is_reported_where_it_is_written_with_the_numbers_it_compared,
    ),
];

fn outcome(source: &str) -> Outcome {
    scalar::run(&lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}")))
}

/// For every function of numbers and `bool` the generator makes, the kernel
/// gives what the program prints, with no limit that is reached.
fn the_kernel_works_a_function_out_to_what_the_program_prints() {
    generate::check(130, 200, program, |p| {
        let Ok(all) = core_terms(p.source.as_bytes()) else {
            return;
        };
        let f = all.iter().find(|f| f.name == "f").expect("a term for f");
        let mut machine = Machine::new(&all, 1_000_000);
        let (got, _) = machine
            .call(f.place, vec![Value::Int(p.a), Value::Int(p.b)])
            .expect("worked out");
        let Value::Int(n) = got else {
            panic!("{got:?} is not a number")
        };
        assert_eq!(
            outcome(&p.source),
            Outcome {
                stdout: format!("{n}\n").into_bytes(),
                status: 0
            }
        );
    });
}

const COUNT: &str = "fn count(n: u64) -> u64\n  decreases n\n{\n  if n == 0 { 0 } else { count(n - 1) }\n}\n\nfn main() ! console, alloc {\n  print(\"{count(3)}\")\n}\n";

fn steps_of(n: i128, limit: u64) -> Result<u64, Failure> {
    let all = core_terms(with_header(COUNT.to_string()).as_bytes()).expect("checks");
    let f = all.iter().find(|f| f.name == "count").expect("a term");
    let mut machine = Machine::new(&all, limit);
    machine.call(f.place, vec![Value::Int(n)])?;
    Ok(machine.steps())
}

/// A function that calls itself takes steps in proportion to how often, and
/// a run given fewer fails with the limit it was given.
fn a_run_past_its_steps_fails_and_the_steps_grow_with_what_it_works_out() {
    let (a, b, c) = (
        steps_of(10, 1_000_000).expect("ends"),
        steps_of(20, 1_000_000).expect("ends"),
        steps_of(30, 1_000_000).expect("ends"),
    );
    assert_eq!(b - a, c - b, "a round costs the same each time");
    assert!(b > a);
    assert_eq!(steps_of(20, b - 1), Err(Failure::Limit(b - 1)));
    assert_eq!(steps_of(20, b), Ok(b));
}

fn why(source: &str, name: &str, args: Vec<i128>) -> (Value, Option<Why>) {
    let all = core_terms(with_header(source.to_string()).as_bytes()).expect("checks");
    let f = all.iter().find(|f| f.name == name).expect("a term");
    Machine::new(&all, 100_000)
        .call(f.place, args.into_iter().map(Value::Int).collect())
        .expect("worked out")
}

fn a_false_comparison_is_reported_where_it_is_written_with_the_numbers_it_compared() {
    let source = "fn inside(i: i32, n: i32) -> bool { i < n }\n\nfn both(i: i32, n: i32) -> bool { inside(i, n) and n < 100 }\n\nfn neither(i: i32, n: i32) -> bool { not (i < n) }\n\nfn main() ! console, alloc {\n  print(\"{inside(1, 2)}{both(1, 2)}{neither(1, 2)}\")\n}\n";
    let (v, w) = why(source, "inside", vec![9, 3]);
    assert_eq!(v, Value::Case(0, vec![]));
    let w = w.expect("a reason");
    assert_eq!(w.text, "`9 < 3` is false");
    assert_eq!(w.pos.line, 1 + 3, "the comparison is on the fourth line, after the header");
    // The second side of an `and` is what made it false, with its own numbers.
    let (v, w) = why(source, "both", vec![1, 200]);
    assert_eq!(v, Value::Case(0, vec![]));
    assert_eq!(w.expect("a reason").text, "`200 < 100` is false");
    // The first side is, when it is.
    let (_, w) = why(source, "both", vec![5, 3]);
    assert_eq!(w.expect("a reason").text, "`5 < 3` is false");
    // A `not` of a comparison that is true is false because of it.
    let (v, w) = why(source, "neither", vec![1, 2]);
    assert_eq!(v, Value::Case(0, vec![]));
    assert_eq!(w.expect("a reason").text, "`1 < 2` is true");
}

#[derive(Debug)]
struct Looped {
    source: String,
    n: i128,
    k: i128,
    xs: Vec<i128>,
}

fn looped(g: &mut Gen) -> Looped {
    let (n, k) = (g.below(8) as i128, g.below(8) as i128);
    let xs: Vec<i128> = (0..g.below(6)).map(|_| g.below(6) as i128).collect();
    let list = xs.iter().map(i128::to_string).collect::<Vec<_>>().join(", ");
    Looped {
        source: with_header(format!(
            "fn find(xs: List[i32], k: i32) -> bool {{\n  var found = false\n  var last: i32 = 0\n  loop v in xs {{\n    last = v\n    if v == k {{\n      found = true\n      break\n    }}\n  }}\n  found\n}}\n\n\
fn first_at(n: u64, k: u64) -> u64 {{\n  var hit: u64 = 0\n  loop i in 0..n {{\n    if i == k {{\n      hit = i + 1\n      break\n    }}\n  }}\n  hit\n}}\n\n\
fn main() ! console, alloc {{\n  xs: List[i32] = [{list}]\n  print(\"{{find(xs, {k})}} {{first_at({n}, {k})}}\")\n}}\n"
        )),
        n,
        k,
        xs,
    }
}

/// For every loop over a list that stops at a `break`, and over a range that
/// does, the kernel hands the `var`s from round to round and gives what the
/// program prints.
fn the_kernel_works_a_loop_out_round_by_round_to_what_the_program_prints() {
    generate::check(131, 150, looped, |p| {
        let Ok(all) = core_terms(p.source.as_bytes()) else {
            return;
        };
        let term = |name: &str| all.iter().find(|f| f.name == name).expect("a term").place;
        let mut machine = Machine::new(&all, 1_000_000);
        let list = Value::Parts(p.xs.iter().map(|x| Value::Int(*x)).collect());
        let (found, _) = machine
            .call(term("find"), vec![list, Value::Int(p.k)])
            .expect("worked out");
        let (hit, _) = machine
            .call(term("first_at"), vec![Value::Int(p.n), Value::Int(p.k)])
            .expect("worked out");
        let Value::Case(found, _) = found else {
            panic!("a bool")
        };
        let Value::Int(hit) = hit else {
            panic!("a number")
        };
        assert_eq!(
            outcome(&p.source),
            Outcome {
                stdout: format!("{} {hit}\n", found == 1).into_bytes(),
                status: 0
            }
        );
    });
}

#[derive(Debug)]
struct Needs {
    source: String,
    holds: bool,
    k: i128,
}

fn needs(g: &mut Gen) -> Needs {
    let xs: Vec<i128> = (0..1 + g.below(5)).map(|_| g.below(6) as i128).collect();
    let k = g.below(6) as i128;
    let list = xs.iter().map(i128::to_string).collect::<Vec<_>>().join(", ");
    Needs {
        source: with_header(format!(
            "fn has(xs: List[i32], k: i32) -> bool {{\n  var found = false\n  loop v in xs {{\n    if v == k {{\n      found = true\n      break\n    }}\n  }}\n  found\n}}\n\n\
fn pick(xs: List[i32], k: i32) -> i32\n  requires has(xs, k)\n{{\n  k\n}}\n\n\
fn main() ! console, alloc {{\n  print(\"{{pick([{list}], {k})}}\")\n}}\n"
        )),
        holds: xs.contains(&k),
        k,
    }
}

/// A call whose `requires` is a predicate with a loop, over arguments written
/// out, compiles exactly when the kernel works the predicate out to `true`,
/// and prints what the program printed without the contract.
fn a_requires_over_arguments_written_out_is_proven_by_working_it_out() {
    generate::check(132, 150, needs, |p| match smith::check(p.source.as_bytes()) {
        Ok(_) => {
            assert!(p.holds, "accepted, though the predicate is false");
            assert_eq!(
                outcome(&p.source),
                Outcome {
                    stdout: format!("{}\n", p.k).into_bytes(),
                    status: 0
                }
            );
        }
        Err(errors) => {
            assert!(!p.holds, "refused, though the predicate is true: {errors:?}");
            assert!(
                errors
                    .iter()
                    .any(|e| e.message.contains("is false") && e.message.contains("`has(")),
                "{errors:?} does not say the call is false"
            );
        }
    });
}

/// A predicate that goes on past the steps given fails the build with the call
/// and the limit named, and never takes it for proven.
fn a_predicate_that_does_not_end_within_the_steps_fails_naming_the_call_and_the_limit() {
    let source = with_header(
        "fn late(n: u64, k: u64) -> bool {\n  var found = false\n  loop i in 0..n {\n    if i == k {\n      found = true\n      break\n    }\n  }\n  found\n}\n\nfn go(n: u64, k: u64) -> u64\n  requires late(n, k)\n{\n  k\n}\n\nfn main() ! console, alloc {\n  print(\"{go(100000000000, 99999999999)}\")\n}\n"
            .to_string(),
    );
    let errors = smith::check(source.as_bytes()).expect_err("refused");
    let message = &errors[0].message;
    assert!(
        message.contains("late(100000000000, 99999999999)") && message.contains("1000000 steps"),
        "{message}"
    );
}
