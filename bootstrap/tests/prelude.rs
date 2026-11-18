//! What of the prelude a program is checked with. The prelude is the types,
//! traits, and implementations the compiler reads with every file, and a
//! program costs what it reaches of it, not what it holds.

use smith::prelude_reached;

use crate::Test;

pub const ONCE: &[Test] = &[
    (
        "a_program_that_uses_nothing_the_prelude_defines_reaches_none_of_it",
        a_program_that_uses_nothing_the_prelude_defines_reaches_none_of_it,
    ),
    (
        "a_function_that_nothing_reaches_changes_what_of_the_prelude_is_reached_by_nothing",
        a_function_that_nothing_reaches_changes_what_of_the_prelude_is_reached_by_nothing,
    ),
    (
        "a_main_that_returns_a_result_reaches_the_functions_and_implementations_that_end_the_program",
        a_main_that_returns_a_result_reaches_the_functions_and_implementations_that_end_the_program,
    ),
];

fn reached(source: &str) -> (usize, usize) {
    prelude_reached(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"))
}

/// Neither a `main` that does nothing nor one that prints a text reaches a
/// function or an implementation of the prelude, so none is checked.
fn a_program_that_uses_nothing_the_prelude_defines_reaches_none_of_it() {
    assert_eq!(reached("fn main() {\n}\n"), (0, 0));
    assert_eq!(
        reached("fn main() ! console {\n  print(\"hi\")\n}\n"),
        (0, 0)
    );
}

/// A function that uses `Fmt` and `Repr`, and that `main` does not call, is
/// not checked, so the prelude items it would reach are not either.
fn a_function_that_nothing_reaches_changes_what_of_the_prelude_is_reached_by_nothing() {
    let main = "fn main() ! console {\n  print(\"hi\")\n}\n";
    let unused = "fn unused(n: u8) -> str ! alloc {\n  \"{n} {repr(n)}\"\n}\n";
    assert_eq!(reached(&format!("{unused}\n{main}")), reached(main));
}

/// What is reached is counted at all: a `main` that returns a `Result` is
/// started by a function of the prelude, which calls the `Exit` of `Result`.
fn a_main_that_returns_a_result_reaches_the_functions_and_implementations_that_end_the_program() {
    let (functions, impls) = reached("fn main() -> Result[(), str] {\n  .ok(())\n}\n");
    assert!(functions > 0 && impls > 0, "{functions} and {impls}");
}
