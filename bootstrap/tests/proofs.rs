//! A function whose result is a statement, a comparison of values written
//! where a type is, is a proof. It is checked whether or not anything reaches
//! it, its statement is a `bool` over its parameters, and it carries no
//! effect. Its body, which the kernel reads, is not built yet.

use smith::check;

use crate::Test;
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

/// For each way a statement is written, the only thing refused is the body,
/// which is not built yet: the statement itself is a `bool`.
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
        assert!(found[0].contains("a proof body"), "{statement}: {found:?}");
    }
    // A name that is no parameter is refused where the statement is written.
    let found = messages(&format!("fn p(n: u64) -> n == m {{ same }}\n\n{MAIN}"));
    assert!(
        found.iter().any(|m| m.contains("there is no name `m`")),
        "{found:?}"
    );
    // A statement is a `bool`: a number is not one.
    let found = messages(&format!("fn p(n: u64) -> n + 1 == 2 and n {{ same }}\n\n{MAIN}"));
    assert!(
        found.iter().any(|m| !m.contains("a proof body")),
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
