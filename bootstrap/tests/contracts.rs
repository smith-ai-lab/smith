//! Generators and properties of `requires` and `ensures`, which `arith`
//! checks when the program is built: a call compiles exactly when the
//! arguments satisfy what the function requires, a function compiles exactly
//! when what it returns satisfies what it ensures, and neither adds an
//! instruction to a program.
//!
//! Every generator works out the answer by trying the values or with the
//! arithmetic of Rust on 128 bits, so it shares nothing with the compiler.

use smith::scalar::{self, Outcome};
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::numbers::{INTS, Int, compare, with_header};

pub const ONCE: &[Test] = &[
    (
        "a_call_compiles_exactly_when_its_arguments_satisfy_every_requires",
        a_call_compiles_exactly_when_its_arguments_satisfy_every_requires,
    ),
    (
        "a_requires_that_calls_a_predicate_is_owed_exactly_as_the_expression_it_returns_is",
        a_requires_that_calls_a_predicate_is_owed_exactly_as_the_expression_it_returns_is,
    ),
    (
        "inside_a_function_its_requires_are_facts",
        inside_a_function_its_requires_are_facts,
    ),
    (
        "a_function_compiles_exactly_when_what_it_returns_satisfies_every_ensures",
        a_function_compiles_exactly_when_what_it_returns_satisfies_every_ensures,
    ),
    (
        "after_a_call_what_it_ensures_is_a_fact_so_calls_chain_exactly_as_far_as_the_numbers_allow_part_1_of_2",
        || after_a_call_what_it_ensures_is_a_fact_so_calls_chain_exactly_as_far_as_the_numbers_allow(0),
    ),
    (
        "after_a_call_what_it_ensures_is_a_fact_so_calls_chain_exactly_as_far_as_the_numbers_allow_part_2_of_2",
        || after_a_call_what_it_ensures_is_a_fact_so_calls_chain_exactly_as_far_as_the_numbers_allow(1),
    ),
    (
        "an_ensures_on_a_function_that_does_not_name_its_result_is_an_error_that_says_so",
        an_ensures_on_a_function_that_does_not_name_its_result_is_an_error_that_says_so,
    ),
    (
        "a_function_with_a_requires_is_refused_wherever_it_is_used_as_a_value",
        a_function_with_a_requires_is_refused_wherever_it_is_used_as_a_value,
    ),
    (
        "contracts_add_no_instruction_to_a_program_that_compiles_without_them",
        contracts_add_no_instruction_to_a_program_that_compiles_without_them,
    ),
];

const COMPARISONS: [&str; 6] = ["<", "<=", ">", ">=", "==", "!="];

fn run(source: &str) -> Outcome {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    scalar::run(&program)
}

/// A value of the type, at one of its limits or next to one, or between.
fn near(g: &mut Gen, ty: Int) -> i128 {
    let d = g.below(5) as i128 - 2;
    match g.below(3) {
        0 => (ty.min() + d.abs()).min(ty.max()),
        1 => (ty.max() - d.abs()).max(ty.min()),
        _ => (d + 3).clamp(ty.min(), ty.max()),
    }
}

/// A statement about the variable `x`, written out, with the function that
/// says for which values it holds.
type Statement = (String, Box<dyn Fn(i128) -> bool>);

fn statement(g: &mut Gen, ty: Int, x: &str, depth: usize) -> Statement {
    if depth > 0 && g.one_in(2) {
        let (a, f) = statement(g, ty, x, depth - 1);
        return match g.below(3) {
            0 => (format!("not ({a})"), Box::new(move |v| !f(v))),
            1 => {
                let (b, h) = statement(g, ty, x, depth - 1);
                (format!("({a}) and ({b})"), Box::new(move |v| f(v) && h(v)))
            }
            _ => {
                let (b, h) = statement(g, ty, x, depth - 1);
                (format!("({a}) or ({b})"), Box::new(move |v| f(v) || h(v)))
            }
        };
    }
    let at = near(g, ty);
    let op = *g.pick(&COMPARISONS);
    if g.one_in(2) {
        (
            format!("{x} {op} {at}"),
            Box::new(move |v| compare(op, v, at)),
        )
    } else {
        // The number on the left, as the variable on the right.
        (
            format!("{at} {op} {x}"),
            Box::new(move |v| compare(op, at, v)),
        )
    }
}

#[derive(Debug)]
struct Call {
    source: String,
    /// Whether the argument satisfies every `requires`.
    holds: bool,
    arg: i128,
}

/// A function with several `requires` over its parameter, and a `main` that
/// calls it with a number, written as a literal or as a name.
fn call(g: &mut Gen) -> Call {
    let ty = *g.pick(INTS);
    let arg = near(g, ty);
    let mut holds = true;
    let mut lines = String::new();
    for _ in 0..1 + g.below(3) {
        let (text, f) = statement(g, ty, "i", 2);
        holds &= f(arg);
        lines.push_str(&format!("  requires {text}\n"));
    }
    let t = ty.name;
    let passed = if g.one_in(2) {
        arg.to_string()
    } else {
        "x".to_string()
    };
    let bind = if passed == "x" {
        format!("  x: {t} = {arg}\n")
    } else {
        String::new()
    };
    Call {
        source: with_header(format!(
            "fn needs(i: {t}) -> {t}\n{lines}{{\n  i\n}}\n\nfn main() ! console, alloc {{\n{bind}  print(\"{{needs({passed})}}\")\n}}\n"
        )),
        holds,
        arg,
    }
}

/// For every function with several `requires`, each a comparison of whole
/// numbers joined with `and`, `or`, and `not`, a call compiles exactly when
/// `arith` proves them of the call's arguments, and prints what it was given.
fn a_call_compiles_exactly_when_its_arguments_satisfy_every_requires() {
    generate::check(110, 150, call, |c| match check(c.source.as_bytes()) {
        Ok(_) => {
            assert!(c.holds, "accepted, though a `requires` does not hold");
            assert_eq!(
                run(&c.source),
                Outcome {
                    stdout: format!("{}\n", c.arg).into_bytes(),
                    status: 0
                }
            );
        }
        Err(errors) => {
            assert!(
                !c.holds,
                "refused, though every `requires` holds: {errors:?}"
            );
            assert!(
                errors.iter().any(|e| e.message.contains("needs")),
                "{errors:?} does not name the function"
            );
        }
    });
}

/// The same as `call`, with each statement written once as a function that
/// returns `bool` and named in the `requires`, some of them through a second
/// function that calls the first.
fn predicates(g: &mut Gen) -> Call {
    let ty = *g.pick(INTS);
    let arg = near(g, ty);
    let t = ty.name;
    let mut holds = true;
    let mut defs = String::new();
    let mut lines = String::new();
    for k in 0..1 + g.below(3) {
        let (text, f) = statement(g, ty, "i", 2);
        holds &= f(arg);
        defs.push_str(&format!("fn p{k}(i: {t}) -> bool {{ {text} }}\n\n"));
        if g.one_in(2) {
            defs.push_str(&format!("fn q{k}(j: {t}) -> bool {{ p{k}(j) }}\n\n"));
            lines.push_str(&format!("  requires q{k}(i)\n"));
        } else {
            lines.push_str(&format!("  requires p{k}(i)\n"));
        }
    }
    let passed = if g.one_in(2) {
        arg.to_string()
    } else {
        "x".to_string()
    };
    let bind = if passed == "x" {
        format!("  x: {t} = {arg}\n")
    } else {
        String::new()
    };
    Call {
        source: with_header(format!(
            "{defs}fn needs(i: {t}) -> {t}\n{lines}{{\n  i\n}}\n\nfn main() ! console, alloc {{\n{bind}  print(\"{{needs({passed})}}\")\n}}\n"
        )),
        holds,
        arg,
    }
}

/// For every function whose `requires` calls predicates, functions with no
/// effect that return a comparison, or a call of another, a call compiles
/// exactly when the comparisons hold of its arguments, as with the
/// expressions written in place, and the program prints the same.
fn a_requires_that_calls_a_predicate_is_owed_exactly_as_the_expression_it_returns_is() {
    generate::check(111, 150, predicates, |c| match check(c.source.as_bytes()) {
        Ok(_) => {
            assert!(c.holds, "accepted, though a predicate does not hold");
            assert_eq!(
                run(&c.source),
                Outcome {
                    stdout: format!("{}\n", c.arg).into_bytes(),
                    status: 0
                }
            );
        }
        Err(errors) => {
            assert!(
                !c.holds,
                "refused, though every predicate holds: {errors:?}"
            );
            assert!(
                errors.iter().any(|e| e.message.contains("needs")),
                "{errors:?} does not name the function"
            );
        }
    });
}

#[derive(Debug)]
struct Inside {
    source: String,
    /// Whether the operation fits for every value the `requires` allows.
    fits: bool,
    /// How the operation is written, for the error.
    written: String,
}

/// A function that requires its parameter to lie between two numbers, and
/// adds a number to it or takes one from it.
fn inside(g: &mut Gen) -> Inside {
    let ty = *g.pick(INTS);
    let (a, b) = (near(g, ty), near(g, ty));
    let (lo, hi) = (a.min(b), a.max(b));
    let k = g.below(4) as i128;
    let add = g.one_in(2);
    let (written, low, high) = if add {
        (format!("i + {k}"), lo + k, hi + k)
    } else {
        (format!("i - {k}"), lo - k, hi - k)
    };
    let fits = low >= ty.min() && high <= ty.max();
    let t = ty.name;
    let upper = if g.one_in(2) {
        format!("i <= {hi}")
    } else {
        format!("not (i > {hi})")
    };
    Inside {
        source: with_header(format!(
            "fn f(i: {t}) -> {t}\n  requires {lo} <= i\n  requires {upper}\n{{\n  {written}\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{f({lo})}}\")\n}}\n"
        )),
        fits,
        written,
    }
}

/// Inside a function the `requires` are facts, so an operation on the
/// parameter compiles exactly when it fits for every value they allow.
fn inside_a_function_its_requires_are_facts() {
    generate::check(111, 150, inside, |c| match check(c.source.as_bytes()) {
        Ok(_) => assert!(c.fits, "accepted, though the result may not fit"),
        Err(errors) => {
            assert!(!c.fits, "refused, though every result fits: {errors:?}");
            assert!(
                errors.iter().any(|e| e.message.contains(&c.written)),
                "{errors:?} does not state the goal about `{}`",
                c.written
            );
        }
    });
}

#[derive(Debug)]
struct Gives {
    source: String,
    /// Whether every `ensures` holds at both places the function returns.
    holds: bool,
}

/// A function that adds one number in one block of an `if` and another in
/// the other, under a `requires` that lets both fit, with several `ensures`
/// about the result and the parameter.
fn gives(g: &mut Gen) -> Gives {
    let ty = *g.pick(INTS);
    let (c1, c2) = (g.below(4) as i128, g.below(4) as i128);
    let t = ty.name;
    let top = ty.max() - 3;
    // The branch is on a comparison of the parameter, whichever way it goes.
    let split = near(g, ty).min(top);
    let mut holds = true;
    let mut lines = String::new();
    for _ in 0..1 + g.below(3) {
        let d = g.below(5) as i128 - 2;
        let op = *g.pick(&COMPARISONS);
        let sign = if d < 0 { "-" } else { "+" };
        lines.push_str(&format!("  ensures r {op} i {sign} {}\n", d.abs()));
        // The block for values below the split is never run when none are.
        holds &= (split <= ty.min() || compare(op, c1, d)) && compare(op, c2, d);
    }
    Gives {
        source: with_header(format!(
            "fn lift(i: {t}) -> r: {t}\n  requires i <= {top}\n{lines}{{\n  if i < {split} {{\n    i + {c1}\n  }} else {{\n    i + {c2}\n  }}\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{lift({})}}\")\n}}\n",
            ty.min()
        )),
        holds,
    }
}

/// A function compiles exactly when `arith` proves every `ensures` of what
/// it returns, at each place it returns.
fn a_function_compiles_exactly_when_what_it_returns_satisfies_every_ensures() {
    generate::check(112, 150, gives, |c| match check(c.source.as_bytes()) {
        Ok(_) => assert!(c.holds, "accepted, though an `ensures` does not hold"),
        Err(errors) => {
            assert!(
                !c.holds,
                "refused, though every `ensures` holds: {errors:?}"
            );
            assert!(
                errors.iter().any(|e| e.message.contains("lift")),
                "{errors:?} does not name the function"
            );
        }
    });
}

#[derive(Debug)]
struct Chain {
    source: String,
    /// Whether the numbers allow the calls.
    fits: bool,
    ty: Int,
    start: i128,
    depth: i128,
}

/// A function whose `ensures` says what it adds, called inside another that
/// calls it `depth` times, one on the result of the other, under a `requires`
/// that is next to the limit.
fn chain(g: &mut Gen) -> Chain {
    let ty = *g.pick(INTS);
    let depth = 1 + g.below(3) as i128;
    let bound = ty.max() - depth + g.below(3) as i128 - 1;
    let start = bound.min(ty.max());
    let t = ty.name;
    let mut call = "i".to_string();
    for _ in 0..depth {
        call = format!("next({call})");
    }
    Chain {
        source: with_header(format!(
            "fn next(i: {t}) -> r: {t}\n  requires i < {max}\n  ensures r == i + 1\n{{\n  i + 1\n}}\n\nfn many(i: {t}) -> {t}\n  requires i <= {bound}\n{{\n  {call}\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{many({start})}}\")\n}}\n",
            max = ty.max()
        )),
        fits: bound + depth <= ty.max(),
        ty,
        start,
        depth,
    }
}

/// After a call, what the function ensures of its arguments and its result
/// is a fact, so `depth` calls one on the result of another compile exactly
/// when the `requires` of the caller leaves room for them, and then add up.
fn after_a_call_what_it_ensures_is_a_fact_so_calls_chain_exactly_as_far_as_the_numbers_allow(part: u64) {
    generate::check_part(113, 150, part, 2, chain, |c| match check(c.source.as_bytes()) {
        Ok(_) => {
            assert!(c.fits, "accepted, though the calls can pass the limit");
            assert_eq!(
                run(&c.source),
                Outcome {
                    stdout: format!("{}\n", c.start + c.depth).into_bytes(),
                    status: 0
                }
            );
        }
        Err(errors) => {
            assert!(!c.fits, "refused, though the calls fit: {errors:?}");
            assert!(
                errors.iter().any(|e| e.message.contains("next")),
                "{errors:?} does not name the function {:?}",
                c.ty
            );
        }
    });
}

/// An `ensures` on a function whose signature gives its result no name is
/// an error that says so, for every type, and naming the result mends it.
fn an_ensures_on_a_function_that_does_not_name_its_result_is_an_error_that_says_so() {
    for ty in INTS {
        let t = ty.name;
        let unnamed = with_header(format!(
            "fn same(i: {t}) -> {t}\n  ensures i == i\n{{\n  i\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{same(1)}}\")\n}}\n"
        ));
        let errors = check(unnamed.as_bytes()).expect_err("a result with no name");
        assert!(
            errors
                .iter()
                .any(|e| e.message.contains("same") && e.message.contains("name")),
            "{errors:?} does not say the result has no name"
        );
        let named = unnamed.replace("-> ", "-> r: ").replace("i == i", "r == i");
        assert!(check(named.as_bytes()).is_ok(), "{named}");
    }
}

/// A function that never needs its contracts to compile, written with and
/// without them.
fn guarded(g: &mut Gen) -> (String, String) {
    let ty = *g.pick(INTS);
    let t = ty.name;
    let at = near(g, ty);
    let (requires, ensures) = match g.below(3) {
        0 => (
            format!("  requires i >= {}\n", ty.min()),
            "  ensures r >= i\n".to_string(),
        ),
        1 => (
            format!("  requires i <= {} and i >= {}\n", ty.max(), ty.min()),
            "  ensures r >= i and r <= i + 1\n".to_string(),
        ),
        _ => (
            String::new(),
            format!("  ensures r > i or r == {}\n", ty.max()),
        ),
    };
    let body = format!(
        "  if i < {} {{\n    i + 1\n  }} else {{\n    i\n  }}\n",
        ty.max()
    );
    let main = format!("fn main() ! console, alloc {{\n  print(\"{{f({at})}}\")\n}}\n");
    let with = format!("fn f(i: {t}) -> r: {t}\n{requires}{ensures}{{\n{body}}}\n\n{main}");
    let without = format!("fn f(i: {t}) -> {t} {{\n{body}}}\n\n{main}");
    let (with, without) = (with_header(with), with_header(without));
    (with, without)
}

/// For every program that compiles with its contracts and without them, the
/// scalar form is the same, so nothing is added for a `requires` or an
/// `ensures`.
fn contracts_add_no_instruction_to_a_program_that_compiles_without_them() {
    generate::check(114, 60, guarded, |(with, without)| {
        let lowered = |source: &str| {
            lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"))
        };
        assert_eq!(lowered(with), lowered(without));
    });
}

/// A function type states no contract, so a function with a `requires` is
/// only called by its name: in every way a function name is passed as a value, for
/// every type, it is refused and the error names it, and with only an
/// `ensures` it is accepted.
fn a_function_with_a_requires_is_refused_wherever_it_is_used_as_a_value() {
    for ty in INTS {
        let t = ty.name;
        let max = ty.max();
        // How the function is used as a value, in `main`.
        let uses = [
            "apply(1, f: lift)",
            "apply(x: 1, f: lift)",
            "apply(1, lift)",
        ];
        for usage in uses {
            for requires in [true, false] {
                let line = if requires {
                    format!("  requires i < {max}\n")
                } else {
                    String::new()
                };
                let source = with_header(format!(
                    "fn lift(i: {t}) -> r: {t}\n{line}  ensures r >= i\n{{\n  if i < {max} {{ i + 1 }} else {{ i }}\n}}\n\n\
                     fn apply(x: {t}, f: fn({t}) -> {t}) -> {t} {{\n  f(x)\n}}\n\n\
                     fn main() ! console, alloc {{\n  print(\"{{{usage}}}\")\n}}\n"
                ));
                match check(source.as_bytes()) {
                    Ok(_) => assert!(
                        !requires,
                        "accepted, though `lift` has a `requires`: {source}"
                    ),
                    Err(errors) => {
                        assert!(
                            requires,
                            "refused, though `lift` has no `requires`: {errors:?}"
                        );
                        assert!(
                            errors.iter().any(|e| e.message.contains("lift")),
                            "{errors:?} does not name the function for {usage}"
                        );
                    }
                }
            }
        }
    }
}
