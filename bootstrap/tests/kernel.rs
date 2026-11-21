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
use crate::generate;
use crate::numbers::with_header;

pub const ONCE: &[Test] = &[
    (
        "the_kernel_works_a_function_out_to_what_the_program_prints",
        the_kernel_works_a_function_out_to_what_the_program_prints,
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
