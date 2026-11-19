//! What of the prelude a program is checked with. The prelude is the types,
//! traits, and implementations the compiler reads with every file, and a
//! program costs what it reaches of it, not what it holds. Its source is
//! `std/prelude.smith`, which `build.rs` divides into items, and an `impl` is
//! lexed and parsed only when a program reaches its trait or a type it is for.

use std::sync::OnceLock;

use smith::{prelude_read, prelude_read_with, prelude_reached};

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
    (
        "a_program_that_uses_nothing_the_prelude_defines_reads_none_of_its_impls",
        a_program_that_uses_nothing_the_prelude_defines_reads_none_of_its_impls,
    ),
    (
        "an_impl_is_read_when_the_program_reaches_its_trait_and_not_otherwise",
        an_impl_is_read_when_the_program_reaches_its_trait_and_not_otherwise,
    ),
    (
        "the_impls_of_iterate_are_read_by_a_program_with_a_loop_over_a_value_and_by_no_other",
        the_impls_of_iterate_are_read_by_a_program_with_a_loop_over_a_value_and_by_no_other,
    ),
    (
        "adding_a_trait_with_a_hundred_impls_changes_what_a_program_that_uses_none_of_them_reads",
        adding_a_trait_with_a_hundred_impls_changes_what_a_program_that_uses_none_of_them_reads,
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
        reached("use std.console (console, print)\n\nfn main() ! console {\n  print(\"hi\")\n}\n"),
        (0, 0)
    );
}

/// A function that uses `Fmt` and `Repr`, and that `main` does not call, is
/// not checked, so the prelude items it would reach are not either.
fn a_function_that_nothing_reaches_changes_what_of_the_prelude_is_reached_by_nothing() {
    let uses = "use std.console (console, print)\n\n";
    let main = "fn main() ! console {\n  print(\"hi\")\n}\n";
    let unused = "fn unused(n: Byte) -> str ! alloc {\n  \"{n} {repr(n)}\"\n}\n";
    assert_eq!(
        reached(&format!("{uses}{unused}\n{main}")),
        reached(&format!("{uses}{main}"))
    );
}

/// What is reached is counted at all: a `main` that returns a `Result` is
/// started by a function of the prelude, which calls the `Exit` of `Result`.
fn a_main_that_returns_a_result_reaches_the_functions_and_implementations_that_end_the_program() {
    let (functions, impls) = reached("fn main() -> Result[(), str] {\n  .ok(())\n}\n");
    assert!(functions > 0 && impls > 0, "{functions} and {impls}");
}

/// The prelude's source, as the repository holds it.
fn prelude_text() -> &'static str {
    static TEXT: OnceLock<String> = OnceLock::new();
    TEXT.get_or_init(|| {
        let path = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../std/prelude.smith");
        std::fs::read_to_string(path).unwrap()
    })
}

fn read(source: &str) -> usize {
    prelude_read(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"))
}

/// The hello program, and a `main` that does nothing, read no `impl`: what
/// `print` needs of the prelude is its types and traits.
fn a_program_that_uses_nothing_the_prelude_defines_reads_none_of_its_impls() {
    assert_eq!(read("fn main() {\n}\n"), 0);
    assert_eq!(
        read("use std.console (console, print)\n\nfn main() ! console { print(\"hello world\") }\n"),
        0
    );
}

/// A program that writes a whole number in a string reaches `Fmt`, and so
/// every `impl` of it, and no other trait's. A function nothing calls
/// reaches nothing, so it does not change what is read.
fn an_impl_is_read_when_the_program_reaches_its_trait_and_not_otherwise() {
    let uses = "use std.console (console, print)\n\n";
    let main = "fn main() ! console, alloc {\n  print(\"{254}\")\n}\n";
    let fmt = read(&format!("{uses}{main}"));
    let wholes = prelude_text().matches("\nimpl Fmt[").count();
    assert!(fmt > 0 && fmt < prelude_text().matches("\nimpl").count(), "{fmt}");
    assert!(
        fmt >= wholes,
        "{fmt} of the {wholes} implementations of Fmt, and what they reach"
    );
    let unused = "fn unused(a: Byte, b: Byte) -> bool {\n  a == b\n}\n\n";
    assert_eq!(read(&format!("{uses}{unused}{main}")), fmt);
}

/// Adding a trait with a hundred `impl`s to a copy of the prelude's source
/// changes nothing that a program that does not name the trait reads.
fn adding_a_trait_with_a_hundred_impls_changes_what_a_program_that_uses_none_of_them_reads() {
    let mut more = String::from("\ntrait Many[T] {\n  fn many(a: T) -> T\n}\n");
    for n in 0..100 {
        more.push_str(&format!(
            "\nimpl Many[Kind{n}] {{\n  fn many(a: Kind{n}) -> Kind{n} {{ a }}\n}}\n"
        ));
    }
    let extended = format!("{}{more}", prelude_text());
    let programs = [
        "fn main() {\n}\n",
        "use std.console (console, print)\n\nfn main() ! console { print(\"hello world\") }\n",
        "use std.console (console, print)\n\nfn main() ! console, alloc {\n  print(\"{254}\")\n}\n",
        "use std.console (console, print)\n\nfn next(i: Byte) -> Byte { if i < 255 { i + 1 } else { i } }\n\nfn main() ! console, alloc {\n  print(\"{next(254)}\")\n}\n",
    ];
    for program in programs {
        let with = |prelude: &str| {
            prelude_read_with(prelude, program.as_bytes())
                .unwrap_or_else(|e| panic!("{e:?} for {program}"))
        };
        assert_eq!(with(&extended), with(prelude_text()), "{program}");
        assert_eq!(with(prelude_text()), read(program), "{program}");
    }
}

/// A `loop` over a value runs the `each` of its `Iterate`, so a program with
/// one reads the `impl`s of `Iterate`, and the same program with a loop over a
/// range reads none of them.
fn the_impls_of_iterate_are_read_by_a_program_with_a_loop_over_a_value_and_by_no_other() {
    let range = "use std.console (console, print)\n\nfn main() ! console {\n  loop i in 0..3 { print(\"x\") }\n}\n";
    assert_eq!(read(range), 0);
    let value = "use std.console (console, print)\n\nfn main() ! console, alloc {\n  xs: List[Byte] = [1]\n  loop v in xs { print(\"x\") }\n}\n";
    assert!(read(value) > 0, "{}", read(value));
}
