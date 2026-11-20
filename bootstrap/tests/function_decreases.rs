//! `decreases` on a function, and what a function that calls itself is
//! asked: it calls itself last, as the last expression of its body or of a
//! block of an `if` or a `match` that is in that place, or as what a `return`
//! gives, and it has a `decreases`. Whether the measure gets smaller at each
//! call is for `arith` to prove, which is not built yet, so no function that
//! calls itself compiles, and what is tested is what each is refused for. The
//! refusals are of the kind that says it is not supported yet, as that is
//! what they are until the proof is built.

use smith::{check, lower};

use crate::Test;

pub const ONCE: &[Test] = &[
    (
        "a_function_may_have_a_decreases_of_one_part_or_more_and_it_changes_nothing_the_program_does",
        a_function_may_have_a_decreases_of_one_part_or_more_and_it_changes_nothing_the_program_does,
    ),
    (
        "a_decreases_is_never_computed_so_the_scalar_form_is_the_same_with_it_and_without",
        a_decreases_is_never_computed_so_the_scalar_form_is_the_same_with_it_and_without,
    ),
    (
        "a_decreases_is_a_whole_number_of_an_unsigned_type_over_the_parameters",
        a_decreases_is_a_whole_number_of_an_unsigned_type_over_the_parameters,
    ),
    (
        "a_function_has_one_decreases",
        a_function_has_one_decreases,
    ),
    (
        "a_function_that_calls_itself_last_is_refused_and_the_error_says_what_it_lacks",
        a_function_that_calls_itself_last_is_refused_and_the_error_says_what_it_lacks,
    ),
    (
        "a_call_of_itself_that_is_not_the_last_thing_a_function_does_is_not_supported_yet",
        a_call_of_itself_that_is_not_the_last_thing_a_function_does_is_not_supported_yet,
    ),
];

/// The messages of the errors a program is refused with, or none if it
/// compiles.
fn refused(source: &str) -> Vec<String> {
    match check(source.as_bytes()) {
        Ok(_) => Vec::new(),
        Err(errors) => errors.into_iter().map(|e| e.message).collect(),
    }
}

/// A program of `count` with `body`, which `main` calls.
fn counting(header: &str, body: &str) -> String {
    format!(
        "use std.bits (u64)\nuse std.console (console, print)\nuse std.int (wrapping_add)\n\nfn count(n: u64, acc: u64) -> u64{header}\n{{\n  {body}\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{count(5, 0)}}\")\n}}\n"
    )
}

const TAIL: &str = "if n == 0 { acc } else { count(n - 1, wrapping_add(acc, 1)) }";

fn a_function_may_have_a_decreases_of_one_part_or_more_and_it_changes_nothing_the_program_does() {
    let plain = counting("", "acc");
    assert_eq!(refused(&plain), Vec::<String>::new());
    for header in [
        "\n  decreases n",
        "\n  decreases n, 1",
        "\n  decreases n, acc",
        "\n  requires n < 9\n  decreases n + 1",
        "\n  decreases n - n",
        "\n  decreases n - acc, 1",
    ] {
        let source = counting(header, "acc");
        assert_eq!(refused(&source), Vec::<String>::new(), "for {source}");
    }
}

fn a_decreases_is_never_computed_so_the_scalar_form_is_the_same_with_it_and_without() {
    let plain = lower(counting("", "wrapping_add(acc, n)").as_bytes()).unwrap();
    for header in ["\n  decreases n", "\n  decreases n, 1", "\n  decreases n + acc"] {
        let with = lower(counting(header, "wrapping_add(acc, n)").as_bytes()).unwrap();
        assert_eq!(with.functions.len(), plain.functions.len(), "{header}");
        for (a, b) in with.functions.iter().zip(&plain.functions) {
            assert_eq!(a.blocks, b.blocks, "{header}");
            assert_eq!(a.types, b.types, "{header}");
        }
    }
}

fn a_decreases_is_a_whole_number_of_an_unsigned_type_over_the_parameters() {
    for (header, wanted) in [
        ("\n  decreases true", "unsigned type"),
        ("\n  decreases 1, true", "unsigned type"),
        ("\n  decreases \"text\"", "unsigned type"),
        ("\n  decreases m", "`m`"),
    ] {
        let source = counting(header, "acc");
        let errors = refused(&source);
        assert!(
            errors.iter().any(|e| e.contains(wanted)),
            "{errors:?} for {source}"
        );
    }
    let signed = "use std.bits (i64, u64)\nuse std.console (console, print)\n\nfn f(n: i64) -> i64\n  decreases n\n{\n  n\n}\n\nfn main() ! console, alloc {\n  print(\"{f(3)}\")\n}\n";
    let errors = refused(signed);
    assert!(errors.iter().any(|e| e.contains("unsigned type")), "{errors:?}");
}

fn a_function_has_one_decreases() {
    let source = counting("\n  decreases n\n  decreases acc", "acc");
    let errors = refused(&source);
    assert!(
        errors.iter().any(|e| e.contains("one `decreases`")),
        "{errors:?} for {source}"
    );
}

fn a_function_that_calls_itself_last_is_refused_and_the_error_says_what_it_lacks() {
    // The last expression, an arm of an `if`, an arm of a `match`, and what a
    // `return` gives.
    let bodies = [
        TAIL.to_string(),
        "if n == 0 { return acc }\n  count(n - 1, wrapping_add(acc, 1))".to_string(),
        "if n == 0 { acc } else { return count(n - 1, wrapping_add(acc, 1)) }".to_string(),
        "match n {\n    0 { acc }\n    _ { count(n - 1, wrapping_add(acc, 1)) }\n  }".to_string(),
    ];
    for body in &bodies {
        let source = counting("", body);
        let errors = refused(&source);
        assert!(
            errors
                .iter()
                .any(|e| e.contains("needs a `decreases`") && e.contains("not supported yet")),
            "{errors:?} for {source}"
        );
        // With one, the call is still refused, as what `arith` proves of it is
        // not built, and no longer for lacking one.
        let with = counting("\n  decreases n", body);
        let errors = refused(&with);
        assert!(
            errors
                .iter()
                .any(|e| e.contains("`decreases`") && e.contains("not supported yet")),
            "{errors:?} for {with}"
        );
        assert!(
            !errors.iter().any(|e| e.contains("needs a `decreases`")),
            "{errors:?} for {with}"
        );
    }
}

fn a_call_of_itself_that_is_not_the_last_thing_a_function_does_is_not_supported_yet() {
    for body in [
        "if n == 0 { acc } else { count(n - 1, acc) + 1 }",
        "x = count(n - 1, acc)\n  x",
        "if n == 0 { acc } else { wrapping_add(count(n - 1, acc), 1) }",
    ] {
        let source = counting("\n  decreases n", body);
        let errors = refused(&source);
        assert!(
            errors
                .iter()
                .any(|e| e.contains("calls itself") && e.contains("not supported yet")),
            "{errors:?} for {source}"
        );
        assert!(
            !errors.iter().any(|e| e.contains("`decreases`")),
            "{errors:?} for {source}"
        );
    }
}
