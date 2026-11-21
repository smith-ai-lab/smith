//! The kernel works a core term out one step at a time. What it gives of a
//! function is what the program prints, a run that goes past its steps fails
//! and names the limit, and a comparison that gave `false` is reported with
//! where it is written and the numbers it compared.

use smith::core_terms;
use smith::kernel::{Failure, Machine, Value, Why};
use smith::lower;
use smith::scalar::{self, Outcome};

use crate::Test;
use crate::core_producer::{BATCH, Program, batch, batch_source};
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
        "the_kernel_works_out_the_size_an_index_the_bytes_and_a_loop_over_a_text",
        the_kernel_works_out_the_size_an_index_the_bytes_and_a_loop_over_a_text,
    ),
    (
        "the_kernel_gives_a_push_one_more_and_a_slice_the_difference_of_its_ends_and_no_slice_outside_the_list",
        the_kernel_gives_a_push_one_more_and_a_slice_the_difference_of_its_ends_and_no_slice_outside_the_list,
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
    generate::check(130, 200 / BATCH as u64, batch, |batch| {
        // A program `arith` refuses is not what is tested: its cases are then
        // each tried alone.
        if !kernel_gives(&batch.cases) {
            for case in &batch.cases {
                kernel_gives(std::slice::from_ref(case));
            }
        }
    });
}

/// Whether the cases are accepted as one program, and if they are, that the
/// kernel gives the line the program prints for each.
fn kernel_gives(cases: &[Program]) -> bool {
    let source = batch_source(cases);
    let Ok(all) = core_terms(source.as_bytes()) else {
        return false;
    };
    let mut expected = String::new();
    for case in cases {
        let name = case.name();
        let f = all.iter().find(|f| f.name == name).expect("a term for f");
        let mut machine = Machine::new(&all, 1_000_000);
        let (got, _) = machine
            .call(f.place, vec![Value::Int(case.a), Value::Int(case.b)])
            .expect("worked out");
        let Value::Int(n) = got else {
            panic!("{got:?} is not a number")
        };
        expected.push_str(&format!("{n}\n"));
    }
    assert_eq!(
        outcome(&source),
        Outcome {
            stdout: expected.into_bytes(),
            status: 0
        },
        "{source}"
    );
    true
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
    assert_eq!(w.text(), "`9 < 3` is false");
    assert_eq!(w.pos.line, 1 + 3, "the comparison is on the fourth line, after the header");
    // The second side of an `and` is what made it false, with its own numbers.
    let (v, w) = why(source, "both", vec![1, 200]);
    assert_eq!(v, Value::Case(0, vec![]));
    assert_eq!(w.expect("a reason").text(), "`200 < 100` is false");
    // The first side is, when it is.
    let (_, w) = why(source, "both", vec![5, 3]);
    assert_eq!(w.expect("a reason").text(), "`5 < 3` is false");
    // A `not` of a comparison that is true is false because of it.
    let (v, w) = why(source, "neither", vec![1, 2]);
    assert_eq!(v, Value::Case(0, vec![]));
    assert_eq!(w.expect("a reason").text(), "`1 < 2` is true");
}

#[derive(Debug)]
struct Looped {
    n: i128,
    k: i128,
    xs: Vec<i128>,
}

#[derive(Debug)]
struct Loopeds(Vec<Looped>);

fn looped(g: &mut Gen) -> Loopeds {
    Loopeds(
        (0..BATCH)
            .map(|_| Looped {
                n: g.below(8) as i128,
                k: g.below(8) as i128,
                xs: (0..g.below(6)).map(|_| g.below(6) as i128).collect(),
            })
            .collect(),
    )
}

/// The program of cases: `find` and `first_at` once, and a call of each for
/// every case.
fn looped_source(cases: &[Looped]) -> String {
    let mut main = String::new();
    for (at, c) in cases.iter().enumerate() {
        let list = c.xs.iter().map(i128::to_string).collect::<Vec<_>>().join(", ");
        main.push_str(&format!(
            "  xs{at}: List[i32] = [{list}]\n  print(\"{{find(xs{at}, {})}} {{first_at({}, {})}}\")\n",
            c.k, c.n, c.k
        ));
    }
    with_header(format!(
        "fn find(xs: List[i32], k: i32) -> bool {{\n  var found = false\n  var last: i32 = 0\n  loop v in xs {{\n    last = v\n    if v == k {{\n      found = true\n      break\n    }}\n  }}\n  found\n}}\n\n\
fn first_at(n: u64, k: u64) -> u64 {{\n  var hit: u64 = 0\n  loop i in 0..n {{\n    if i == k {{\n      hit = i + 1\n      break\n    }}\n  }}\n  hit\n}}\n\n\
fn main() ! console, alloc {{\n{main}}}\n"
    ))
}

/// For every loop over a list that stops at a `break`, and over a range that
/// does, the kernel hands the `var`s from round to round and gives what the
/// program prints.
fn the_kernel_works_a_loop_out_round_by_round_to_what_the_program_prints() {
    generate::check(131, 150 / BATCH as u64, looped, |p| {
        if !loops_given(&p.0) {
            for case in &p.0 {
                loops_given(std::slice::from_ref(case));
            }
        }
    });
}

fn loops_given(cases: &[Looped]) -> bool {
    let source = looped_source(cases);
    let Ok(all) = core_terms(source.as_bytes()) else {
        return false;
    };
    let term = |name: &str| all.iter().find(|f| f.name == name).expect("a term").place;
    let mut machine = Machine::new(&all, 1_000_000);
    let mut expected = String::new();
    for p in cases {
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
        expected.push_str(&format!("{} {hit}\n", found == 1));
    }
    assert_eq!(
        outcome(&source),
        Outcome {
            stdout: expected.into_bytes(),
            status: 0
        },
        "{source}"
    );
    true
}

#[derive(Debug)]
struct Need {
    list: Vec<i128>,
    k: i128,
}

#[derive(Debug)]
struct Needs(Vec<Need>);

fn needs(g: &mut Gen) -> Needs {
    Needs(
        (0..BATCH)
            .map(|_| Need {
                list: (0..1 + g.below(5)).map(|_| g.below(6) as i128).collect(),
                k: g.below(6) as i128,
            })
            .collect(),
    )
}

impl Need {
    fn holds(&self) -> bool {
        self.list.contains(&self.k)
    }

    /// The call, as it is written.
    fn call(&self) -> String {
        let list = self.list.iter().map(i128::to_string).collect::<Vec<_>>().join(", ");
        format!("pick([{list}], {})", self.k)
    }
}

/// The program that calls `pick` for each case.
fn needs_source(cases: &[&Need]) -> String {
    let prints: String = cases
        .iter()
        .map(|c| format!("  print(\"{{{}}}\")\n", c.call()))
        .collect();
    with_header(format!(
        "fn has(xs: List[i32], k: i32) -> bool {{\n  var found = false\n  loop v in xs {{\n    if v == k {{\n      found = true\n      break\n    }}\n  }}\n  found\n}}\n\n\
fn pick(xs: List[i32], k: i32) -> i32\n  requires has(xs, k)\n{{\n  k\n}}\n\n\
fn main() ! console, alloc {{\n{prints}}}\n"
    ))
}

/// A call whose `requires` is a predicate with a loop, over arguments written
/// out, compiles exactly when the kernel works the predicate out to `true`,
/// and prints what the program printed without the contract. The cases that
/// hold are one program, which prints each `k`; the cases that do not are
/// another, which is refused with an error for each.
fn a_requires_over_arguments_written_out_is_proven_by_working_it_out() {
    generate::check(132, 150 / BATCH as u64, needs, |p| {
        let (holding, failing): (Vec<&Need>, Vec<&Need>) = p.0.iter().partition(|c| c.holds());
        if !holding.is_empty() {
            let source = needs_source(&holding);
            let built = smith::check(source.as_bytes());
            assert!(built.is_ok(), "{source}: {:?}", built.err());
            assert_eq!(
                outcome(&source),
                Outcome {
                    stdout: holding.iter().map(|c| format!("{}\n", c.k)).collect::<String>().into_bytes(),
                    status: 0
                }
            );
        }
        if !failing.is_empty() {
            let source = needs_source(&failing);
            let errors = smith::check(source.as_bytes()).expect_err("refused");
            for case in &failing {
                let call = format!("`has({}", &case.call()["pick(".len()..case.call().rfind(", ").unwrap()]);
                assert!(
                    errors
                        .iter()
                        .any(|e| e.message.contains("is false") && e.message.contains(&call)),
                    "{errors:?} does not say the call of {} is false",
                    case.call()
                );
            }
            assert_eq!(errors.len(), failing.len(), "{errors:?}");
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

#[derive(Debug)]
struct Text {
    text: Vec<u8>,
    xs: Vec<i128>,
    k: i128,
    byte: u8,
}

#[derive(Debug)]
struct Texts(Vec<Text>);

fn texts(g: &mut Gen) -> Texts {
    Texts(
        (0..BATCH)
            .map(|_| Text {
                text: (0..g.below(6)).map(|_| b'a' + g.below(4) as u8).collect(),
                xs: (0..g.below(6)).map(|_| g.below(5) as i128).collect(),
                k: g.below(5) as i128,
                byte: b'a' + g.below(4) as u8,
            })
            .collect(),
    )
}

/// The program of cases: the three functions once, and a call of each for every
/// case.
fn texts_source(cases: &[Text]) -> String {
    let mut main = String::new();
    for (at, c) in cases.iter().enumerate() {
        let list = c.xs.iter().map(i128::to_string).collect::<Vec<_>>().join(", ");
        let word = String::from_utf8(c.text.clone()).expect("ascii");
        main.push_str(&format!(
            "  xs{at}: List[i32] = [{list}]\n  word{at} = \"{word}\"\n  print(\"{{first_index(xs{at}, {})}} {{has_byte(word{at}, {})}} {{length(word{at})}}\")\n",
            c.k, c.byte
        ));
    }
    with_header(format!(
        "fn first_index(xs: List[i32], k: i32) -> u64 {{\n  var hit: u64 = 0\n  loop i in 0..xs.size {{\n    if xs[i] == k {{\n      hit = i\n      break\n    }}\n  }}\n  hit\n}}\n\n\
fn has_byte(t: str, b: Byte) -> bool {{\n  var found = false\n  loop c in t {{\n    if c == b {{\n      found = true\n      break\n    }}\n  }}\n  found\n}}\n\n\
fn length(t: str) -> u64 {{ t.size }}\n\n\
fn main() ! console, alloc {{\n{main}}}\n"
    ))
}

/// For every list searched by index, text searched by byte, and text measured,
/// the kernel gives what the program prints.
fn the_kernel_works_out_the_size_an_index_the_bytes_and_a_loop_over_a_text() {
    generate::check(133, 150 / BATCH as u64, texts, |p| {
        let source = texts_source(&p.0);
        let all = core_terms(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
        let term = |name: &str| all.iter().find(|f| f.name == name).expect("a term").place;
        let mut machine = Machine::new(&all, 1_000_000);
        let mut expected = String::new();
        for c in &p.0 {
            let list = Value::Parts(c.xs.iter().map(|x| Value::Int(*x)).collect());
            let (hit, _) = machine
                .call(term("first_index"), vec![list, Value::Int(c.k)])
                .expect("worked out");
            let (found, _) = machine
                .call(
                    term("has_byte"),
                    vec![Value::Text(c.text.clone()), Value::Int(c.byte as i128)],
                )
                .expect("worked out");
            let (len, _) = machine
                .call(term("length"), vec![Value::Text(c.text.clone())])
                .expect("worked out");
            let (Value::Int(hit), Value::Case(found, _), Value::Int(len)) = (hit, found, len)
            else {
                panic!("values")
            };
            expected.push_str(&format!("{hit} {} {len}\n", found == 1));
        }
        assert_eq!(
            outcome(&source),
            Outcome {
                stdout: expected.into_bytes(),
                status: 0
            },
            "{source}"
        );
    });
}

#[derive(Debug)]
struct Cut {
    xs: Vec<i128>,
    x: i128,
    a: u64,
    b: u64,
}

/// A list of up to five bytes, with a push and a range inside it, which may be
/// empty and may start where it ends.
fn cut(g: &mut Gen) -> Cut {
    let xs: Vec<i128> = (0..g.below(6)).map(|_| g.below(200) as i128).collect();
    let a = g.below(xs.len() + 1);
    let b = a + g.below(xs.len() + 1 - a);
    Cut {
        xs,
        x: g.below(200) as i128,
        a: a as u64,
        b: b as u64,
    }
}

/// The laws of a push and a slice, which the proofs use, are what the
/// evaluator gives for any list: a push has one more element, the last the one
/// pushed, and a slice has the elements from `a` up to `b`, so `b - a` of them.
/// A range that is not inside the list is no slice, so no size comes of it.
fn the_kernel_gives_a_push_one_more_and_a_slice_the_difference_of_its_ends_and_no_slice_outside_the_list()
{
    let all = core_terms(crate::proofs::LISTS.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
    generate::check(134, 200, cut, |p| {
        let term = |name: &str| all.iter().find(|f| f.name == name).expect("a term").place;
        let mut machine = Machine::new(&all, 1_000_000);
        let list = |items: &[i128]| Value::Parts(items.iter().map(|x| Value::Int(*x)).collect());
        let (grown, _) = machine
            .call(term("grown"), vec![list(&p.xs), Value::Int(p.x)])
            .expect("worked out");
        let mut pushed = p.xs.clone();
        pushed.push(p.x);
        assert_eq!(grown, list(&pushed));
        let (a, b) = (p.a as usize, p.b as usize);
        let (size, _) = machine
            .call(
                term("cut"),
                vec![list(&p.xs), Value::Int(p.a as i128), Value::Int(p.b as i128)],
            )
            .expect("worked out");
        assert_eq!(size, Value::Int((b - a) as i128));
        // A range that starts after it ends, or ends past the list.
        for (from, to) in [(p.xs.len() as i128 + 1, 0), (0, p.xs.len() as i128 + 1)] {
            let refused = machine.call(
                term("cut"),
                vec![list(&p.xs), Value::Int(from), Value::Int(to)],
            );
            assert!(matches!(refused, Err(Failure::Stuck(_))), "{refused:?}");
        }
    });
}
