//! A function whose result is a statement, a comparison of values written
//! where a type is, is a proof. It is checked whether or not anything reaches
//! it, its statement is a `bool` over its parameters, and it carries no
//! effect. Its body is steps, which the kernel checks: `same` proves `a == a`,
//! and a statement with nothing unknown by working it out.

use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::numbers::with_header;

pub const ONCE: &[Test] = &[
    (
        "a_statement_is_read_where_a_result_type_is_written_and_is_checked_over_the_parameters",
        a_statement_is_read_where_a_result_type_is_written_and_is_checked_over_the_parameters,
    ),
    (
        "a_proof_function_is_checked_though_no_code_reaches_it",
        a_proof_function_is_checked_though_no_code_reaches_it,
    ),
    (
        "same_proves_a_statement_with_nothing_unknown_exactly_when_the_evaluator_gives_true",
        same_proves_a_statement_with_nothing_unknown_exactly_when_the_evaluator_gives_true,
    ),
    (
        "same_proves_a_equal_to_a_and_nothing_else_that_has_a_parameter_in_it",
        same_proves_a_equal_to_a_and_nothing_else_that_has_a_parameter_in_it,
    ),
    (
        "a_proof_splits_into_cases_with_an_if_and_each_block_proves_the_statement",
        a_proof_splits_into_cases_with_an_if_and_each_block_proves_the_statement,
    ),
    (
        "arith_proves_what_an_unfolded_body_says_exactly_when_the_numbers_agree",
        arith_proves_what_an_unfolded_body_says_exactly_when_the_numbers_agree,
    ),
    (
        "arith_proves_an_identity_of_products_and_refuses_one_that_is_not",
        arith_proves_an_identity_of_products_and_refuses_one_that_is_not,
    ),
    (
        "unfold_takes_the_block_of_the_case_it_is_in_and_asks_for_one_it_is_not",
        unfold_takes_the_block_of_the_case_it_is_in_and_asks_for_one_it_is_not,
    ),
    (
        "a_proof_that_calls_itself_on_a_smaller_value_proves_gauss_and_the_sum_of_a_double",
        a_proof_that_calls_itself_on_a_smaller_value_proves_gauss_and_the_sum_of_a_double,
    ),
    (
        "a_proof_that_changes_a_step_is_refused_and_says_the_statement_owed",
        a_proof_that_changes_a_step_is_refused_and_says_the_statement_owed,
    ),
    (
        "a_proof_function_is_in_no_executable",
        a_proof_function_is_in_no_executable,
    ),
    (
        "a_result_that_is_a_type_is_read_as_a_type_still",
        a_result_that_is_a_type_is_read_as_a_type_still,
    ),
];

fn messages(source: &str) -> Vec<String> {
    match check(with_header(source.to_string()).as_bytes()) {
        Ok(_) => Vec::new(),
        Err(errors) => errors.into_iter().map(|e| e.message).collect(),
    }
}

const MAIN: &str = "fn main() ! console, alloc { print(\"{1}\") }\n";

/// For each way a statement is written, what is refused is what `same` cannot
/// prove of it, and nothing about how it is written: the statement is a `bool`.
fn a_statement_is_read_where_a_result_type_is_written_and_is_checked_over_the_parameters() {
    for statement in [
        "n == 0",
        "n + 1 == 1 + n",
        "n < 10 and n >= 0",
        "n < 10 or n > 20",
        "not (n == 3)",
        "2 * n == n + n",
    ] {
        let found = messages(&format!("fn p(n: u64) -> {statement} {{ same }}\n\n{MAIN}"));
        assert_eq!(found.len(), 1, "{statement}: {found:?}");
        assert!(found[0].contains("`same` proves"), "{statement}: {found:?}");
    }
    // A name that is no parameter is refused where the statement is written.
    let found = messages(&format!("fn p(n: u64) -> n == m {{ same }}\n\n{MAIN}"));
    assert!(
        found.iter().any(|m| m.contains("there is no name `m`")),
        "{found:?}"
    );
    // A statement is a `bool`: a number is not one.
    let found = messages(&format!(
        "fn p(n: u64) -> n + 1 == 2 and n {{ same }}\n\n{MAIN}"
    ));
    assert!(
        found.iter().any(|m| !m.contains("`same` proves")),
        "{found:?}"
    );
}

/// A function that no code calls is not checked, but a proof is.
fn a_proof_function_is_checked_though_no_code_reaches_it() {
    let found = messages(&format!(
        "fn plain(n: u64) -> u64 {{ n + missing }}\n\nfn p(n: u64) -> n == no_such {{ same }}\n\n{MAIN}"
    ));
    assert!(
        found.iter().any(|m| m.contains("no name `no_such`")),
        "{found:?}"
    );
}

/// A result that is not a comparison is read as the type it is, so no program
/// that built before is read another way.
fn a_result_that_is_a_type_is_read_as_a_type_still() {
    let found = messages(
        "fn a(n: u64) -> u64 { n }\n\nfn b(n: u64) -> List[u64] ! alloc { [n] }\n\nfn c(n: u64) -> r: u64 ensures r == n { n }\n\nfn main() ! console, alloc {\n  print(\"{a(1)}\")\n  print(\"{b(1).size}\")\n  print(\"{c(1)}\")\n}\n"
    );
    assert!(found.is_empty(), "{found:?}");
}

/// A comparison as it is written, and what it says of two numbers.
type Comparison = (&'static str, fn(i128, i128) -> bool);

const OPERATIONS: [Comparison; 6] = [
    ("<", |a, b| a < b),
    ("<=", |a, b| a <= b),
    (">", |a, b| a > b),
    (">=", |a, b| a >= b),
    ("==", |a, b| a == b),
    ("!=", |a, b| a != b),
];

#[derive(Debug)]
struct Closed {
    source: String,
    /// How many of its two proofs prove something false.
    false_ones: usize,
}

/// Two proofs by `same` of a statement that has no parameter: one over a sum of
/// two numbers, and one over a call of a function of the file.
fn closed(g: &mut Gen) -> Closed {
    let (x, y, z) = (g.below(20) as i128, g.below(20) as i128, g.below(20) as i128);
    let (op, holds) = OPERATIONS[g.below(6)];
    // `identity` gives its argument, so the second proof says the same of `x`.
    Closed {
        source: format!(
            "fn identity(n: u64) -> u64 {{ n }}\n\nfn p() -> {x} + {y} {op} {z} {{ same }}\n\nfn q() -> identity({x}) {op} {z} {{ same }}\n\n{MAIN}"
        ),
        false_ones: usize::from(!holds(x + y, z)) + usize::from(!holds(x, z)),
    }
}

/// `same` proves a statement with nothing unknown in it exactly when the
/// evaluator gives `true`, and says that it is false where it is not.
fn same_proves_a_statement_with_nothing_unknown_exactly_when_the_evaluator_gives_true() {
    generate::check(140, 150, closed, |c| {
        let found = messages(&c.source);
        let false_ones = found.iter().filter(|m| m.contains("which is false")).count();
        assert_eq!(false_ones, c.false_ones, "{found:?}");
        assert_eq!(found.len(), c.false_ones, "{found:?}");
    });
}

/// `a == a` is proven with a parameter in it, whatever `a` is, and a statement
/// that has a parameter and is not that is refused naming the parameter.
fn same_proves_a_equal_to_a_and_nothing_else_that_has_a_parameter_in_it() {
    for same in ["n == n", "n + 1 == n + 1", "n * 2 + 3 == n * 2 + 3"] {
        let found = messages(&format!("fn p(n: u64) -> {same} {{ same }}\n\n{MAIN}"));
        assert!(found.is_empty(), "{same}: {found:?}");
    }
    for other in ["n == 1 + n - 1", "n + 1 == 1 + n", "n >= n"] {
        let found = messages(&format!("fn p(n: u64) -> {other} {{ same }}\n\n{MAIN}"));
        assert_eq!(found.len(), 1, "{other}: {found:?}");
        assert!(found[0].contains("`n` unknown"), "{other}: {found:?}");
    }
}

/// An `if` splits a proof into cases, and each block proves the statement.
fn a_proof_splits_into_cases_with_an_if_and_each_block_proves_the_statement() {
    let found = messages(&format!(
        "fn p(n: u64) -> n == n {{\n  if n == 0 {{ same }} else {{ same }}\n}}\n\n{MAIN}"
    ));
    assert!(found.is_empty(), "{found:?}");
    let found = messages(&format!(
        "fn p(n: u64) -> n == 0 {{\n  if n == 0 {{ same }} else {{ same }}\n}}\n\n{MAIN}"
    ));
    // No block proves it, as the cases do not use what they split on yet, and
    // the first one that does not is what is said.
    assert_eq!(found.len(), 1, "{found:?}");
    assert!(found[0].contains("`n == 0` has `n` unknown"), "{found:?}");
}

#[derive(Debug)]
struct Linear {
    source: String,
    right: bool,
}

/// `line(n)` is `a * n + b`, and a proof by `unfold` and `arith` says so, or
/// says it is `a * n + b + 1`, which is not so.
fn linear(g: &mut Gen) -> Linear {
    let (a, b) = (g.below(9), g.below(9));
    let right = g.one_in(2);
    let said = if right {
        format!("{a} * n + {b}")
    } else {
        format!("{a} * n + {b} + 1")
    };
    Linear {
        source: format!(
            "fn line(n: Byte) -> Int {{ {a} * n + {b} }}\n\nfn p(n: Byte) -> line(n) == {said} {{\n  opened = unfold(line(n))\n  arith(opened)\n}}\n\n{MAIN}"
        ),
        right,
    }
}

/// The body of a function is what `unfold` gives, and `arith` gets a statement
/// from it exactly when the two sides differ by what it says.
fn arith_proves_what_an_unfolded_body_says_exactly_when_the_numbers_agree() {
    generate::check(141, 150, linear, |c| {
        let found = messages(&c.source);
        if c.right {
            assert!(found.is_empty(), "{found:?}");
        } else {
            assert_eq!(found.len(), 1, "{found:?}");
            assert!(found[0].contains("`arith` cannot get `line(n) =="), "{found:?}");
            assert!(found[0].contains("`opened`"), "{found:?}");
        }
    });
}

/// A product of unknowns is one unknown of its own, and `arith` multiplies out.
fn arith_proves_an_identity_of_products_and_refuses_one_that_is_not() {
    for (statement, holds) in [
        ("(n + 1) * (n + 1) == n * n + 2 * n + 1", true),
        ("(a + b) * (a + b) == a * a + 2 * a * b + b * b", true),
        ("a * (b + 1) == a * b + a", true),
        ("(n + 1) * (n + 1) == n * n + 2 * n + 2", false),
        ("a * b == b * a + 1", false),
    ] {
        let found = messages(&format!(
            "fn p(n: u64, a: u64, b: u64) -> {statement} {{ arith() }}\n\n{MAIN}"
        ));
        assert_eq!(found.is_empty(), holds, "{statement}: {found:?}");
    }
}

/// `unfold` takes the block of a case the proof is in, and asks for one when
/// the proof is not in any.
fn unfold_takes_the_block_of_the_case_it_is_in_and_asks_for_one_it_is_not() {
    let body = "fn pick(n: u64) -> u64 {\n  if n == 0 { 0 } else { n }\n}\n\n";
    let found = messages(&format!(
        "{body}fn p(n: u64) -> pick(n) == n {{\n  if n == 0 {{\n    same\n  }} else {{\n    opened = unfold(pick(n))\n    arith(opened)\n  }}\n}}\n\n{MAIN}"
    ));
    assert!(found.is_empty(), "{found:?}");
    // Outside the case, which block is taken is not known.
    let found = messages(&format!(
        "{body}fn p(n: u64) -> pick(n) == n {{\n  opened = unfold(pick(n))\n  arith(opened)\n}}\n\n{MAIN}"
    ));
    assert_eq!(found.len(), 1, "{found:?}");
    assert!(found[0].contains("`unfold` needs `n == 0`"), "{found:?}");
    // In the case where `n` is 0, `same` works the statement out for it.
    let found = messages(&format!(
        "{body}fn p(n: u64) -> pick(n) == n + 1 {{\n  if n == 0 {{\n    same\n  }} else {{\n    opened = unfold(pick(n))\n    arith(opened)\n  }}\n}}\n\n{MAIN}"
    ));
    assert!(found.iter().any(|m| m.contains("which is false")), "{found:?}");
    // A name that is no proof is not handed to `arith`.
    let found = messages(&format!(
        "{body}fn p(n: u64) -> pick(n) == n {{ arith(nothing_here) }}\n\n{MAIN}"
    ));
    assert!(found.iter().any(|m| m.contains("names of proofs")), "{found:?}");
}

const INDUCTION: &str = include_str!("cases/proof_induction.smith");

/// `gauss` of `statements.md` and the sum of a double compile, each a proof
/// function that calls itself on a smaller value with its `decreases`.
fn a_proof_that_calls_itself_on_a_smaller_value_proves_gauss_and_the_sum_of_a_double() {
    let found = match check(INDUCTION.as_bytes()) {
        Ok(_) => Vec::new(),
        Err(errors) => errors.into_iter().map(|e| e.message).collect(),
    };
    assert!(found.is_empty(), "{found:?}");
}

fn refused(change: impl Fn(&str) -> String) -> Vec<String> {
    match check(change(INDUCTION).as_bytes()) {
        Ok(_) => Vec::new(),
        Err(errors) => errors.into_iter().map(|e| e.message).collect(),
    }
}

/// A body with any one step changed to give another statement is refused,
/// and a call that is not on a smaller value is, as for any function.
fn a_proof_that_changes_a_step_is_refused_and_says_the_statement_owed() {
    // The statement is another one: the case of 0 is where it is false.
    let found = refused(|s| {
        s.replace(
            "2 * sum_to(n) == n * (n + 1)\n",
            "2 * sum_to(n) == n * (n + 1) + 1\n",
        )
    });
    assert!(found.iter().any(|m| m.contains("which is false")), "{found:?}");
    // `arith` is not handed what the step after needs.
    let found = refused(|s| s.replace("arith(earlier, opened)", "arith(opened)"));
    assert!(
        found
            .iter()
            .any(|m| m.contains("`arith` cannot get `(2 * sum_to(n))")),
        "{found:?}"
    );
    // The statement that is unfolded is for another number.
    let found = refused(|s| s.replace("unfold(sum_to(n))", "unfold(sum_to(n - 1))"));
    assert!(
        found.iter().any(|m| m.contains("`unfold` needs `(n - 1) == 0`")),
        "{found:?}"
    );
    // A call of the proof on a value that is not smaller does not end.
    let found = refused(|s| s.replace("earlier = gauss(n - 1)", "earlier = gauss(n)"));
    assert!(
        found.iter().any(|m| m.contains("`n` is smaller at this call")),
        "{found:?}"
    );
    // A proof that calls itself and has no `decreases` is refused.
    let found = refused(|s| {
        s.replace(
            "  requires n >= 0 and n <= 255\n  decreases n\n{\n  if n == 0 {\n    same",
            "  requires n >= 0 and n <= 255\n{\n  if n == 0 {\n    same",
        )
    });
    assert!(
        found.iter().any(|m| m.contains("decreases")),
        "{found:?}"
    );
}

/// A proof is erased: the program is the same with the proofs and without.
fn a_proof_function_is_in_no_executable() {
    let without: String = INDUCTION
        .split("\n\n")
        .filter(|part| !part.starts_with("fn twice_is_sum") && !part.starts_with("fn gauss"))
        .collect::<Vec<_>>()
        .join("\n\n");
    let with = lower(INDUCTION.as_bytes()).expect("builds");
    let alone = lower(without.as_bytes()).expect("builds");
    // Where each instruction was written is the one thing that differs, as
    // the lines of the proofs are not in the program that has none.
    let unplaced = |text: String| {
        let mut out = String::new();
        let mut rest = text.as_str();
        while let Some(at) = rest.find("Place {") {
            out.push_str(&rest[..at]);
            rest = &rest[at..];
            rest = &rest[rest.find('}').map_or(rest.len(), |end| end + 1)..];
        }
        out.push_str(rest);
        out
    };
    assert_eq!(unplaced(format!("{with:?}")), unplaced(format!("{alone:?}")));
}
