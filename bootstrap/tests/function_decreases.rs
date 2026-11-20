//! `decreases` on a function, and what a function that calls itself is
//! asked: it calls itself last, as the last expression of its body or of a
//! block of an `if` or a `match` that is in that place, or as what a `return`
//! gives, and it has a `decreases` that `arith` proves is not below zero and
//! smaller at each of the calls. Such a function is a loop in the scalar form.
//! A call that is not the last thing a function does, and functions that call
//! each other, are not supported yet.

use smith::scalar::{self, Op, Outcome};
use smith::{check, lower};

use crate::Test;
use crate::targets::{runnable, runs_as};

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
        "a_function_that_calls_itself_last_needs_a_decreases",
        a_function_that_calls_itself_last_needs_a_decreases,
    ),
    (
        "a_function_whose_decreases_is_smaller_at_each_call_of_itself_is_a_loop_that_does_what_the_calls_did",
        a_function_whose_decreases_is_smaller_at_each_call_of_itself_is_a_loop_that_does_what_the_calls_did,
    ),
    (
        "a_call_of_itself_where_the_decreases_is_not_proven_smaller_is_refused_with_the_goal",
        a_call_of_itself_where_the_decreases_is_not_proven_smaller_is_refused_with_the_goal,
    ),
    (
        "a_decreases_of_two_parts_is_compared_as_words_in_a_dictionary_are",
        a_decreases_of_two_parts_is_compared_as_words_in_a_dictionary_are,
    ),
    (
        "a_call_of_itself_that_is_not_the_last_thing_a_function_does_is_not_supported_yet",
        a_call_of_itself_that_is_not_the_last_thing_a_function_does_is_not_supported_yet,
    ),
];

pub const MACHINE: &[Test] = &[(
    "an_executable_of_a_function_that_calls_itself_fifty_million_times_prints_the_count_and_its_stack_does_not_grow",
    an_executable_of_a_function_that_calls_itself_fifty_million_times_prints_the_count_and_its_stack_does_not_grow,
)];

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

/// The four places a call of itself may be the last thing `count` does: the
/// last expression, an arm of an `if`, an arm of a `match`, and what a
/// `return` gives.
fn bodies() -> [String; 4] {
    [
        TAIL.to_string(),
        "if n == 0 { return acc }\n  count(n - 1, wrapping_add(acc, 1))".to_string(),
        "if n == 0 { acc } else { return count(n - 1, wrapping_add(acc, 1)) }".to_string(),
        "match n {\n    0 { acc }\n    _ { count(n - 1, wrapping_add(acc, 1)) }\n  }".to_string(),
    ]
}

fn a_function_that_calls_itself_last_needs_a_decreases() {
    for body in &bodies() {
        let source = counting("", body);
        let errors = refused(&source);
        assert!(
            errors.iter().any(|e| e.contains("needs a `decreases`")),
            "{errors:?} for {source}"
        );
        // It is an error that says what to write, and not one of what is not
        // built.
        assert!(
            !errors.iter().any(|e| e.contains("not supported yet")),
            "{errors:?} for {source}"
        );
    }
}

/// Whether the function `count` of the program calls itself.
fn calls_itself(program: &scalar::Program, name: &str) -> bool {
    program.functions.iter().enumerate().any(|(k, f)| {
        f.name.ends_with(name)
            && (f.blocks.iter().flat_map(|b| &b.instructions))
                .any(|i| matches!(&i.op, Op::Call { function, .. } if *function == k))
    })
}

fn a_function_whose_decreases_is_smaller_at_each_call_of_itself_is_a_loop_that_does_what_the_calls_did(
) {
    // A `match` on the number gives `arith` nothing to prove from in its second
    // arm, so it is the last of the four and is not asked for a proof here.
    for body in &bodies()[..3] {
        let source = counting("\n  decreases n", body);
        assert_eq!(refused(&source), Vec::<String>::new(), "for {source}");
        for n in [0, 1, 5, 40] {
            let source = source.replace("count(5, 0)", &format!("count({n}, 0)"));
            let lowered = lower(source.as_bytes()).unwrap();
            // `lower` makes a loop of a call that is the last thing done.
            assert!(!calls_itself(&lowered, "count"), "{source}");
            let passed = scalar::passes(&lowered);
            assert!(!calls_itself(&passed, "count"), "{source}");
            let said = scalar::run(&lowered);
            assert_eq!(said.stdout, format!("{n}\n").into_bytes(), "{source}");
            assert_eq!(scalar::run(&passed), said, "{source}");
        }
    }
}

/// A call of `count` from itself with the arguments `args`, under the
/// measure `decreases`.
fn calling(decreases: &str, args: &str) -> String {
    counting(
        decreases,
        &format!("if n == 0 {{ acc }} else {{ count({args}) }}"),
    )
}

fn a_call_of_itself_where_the_decreases_is_not_proven_smaller_is_refused_with_the_goal() {
    for (decreases, args, goal) in [
        // The measure stays where it was.
        ("\n  decreases n", "n, acc", "`n` is smaller at this call"),
        ("\n  decreases n", "n - 0, acc", "`n` is smaller at this call"),
        // It goes up.
        ("\n  decreases n", "n, acc", "`n` is smaller at this call"),
        // It is a measure that nothing says is smaller.
        ("\n  decreases acc", "n - 1, wrapping_add(acc, 1)", "`acc` is smaller at this call"),
        ("\n  decreases n - n", "n - 1, acc", "`n - n` is smaller at this call"),
    ] {
        let source = calling(decreases, args);
        let errors = refused(&source);
        assert!(
            errors
                .iter()
                .any(|e| e.contains(goal) && e.contains("`count` may not end")),
            "{errors:?} for {source}"
        );
    }
    // The goal that fails when the measure may go below zero is said too, as a
    // call that does not check what it takes off.
    let below = counting(
        "\n  decreases n",
        "if n == 1 { acc } else { count(n - 2, acc) }",
    );
    let errors = refused(&below);
    assert!(errors.iter().any(|e| e.contains("may not")), "{errors:?} for {below}");
}

/// `f(n, m)` that ends when `m` goes down to 0 and then takes one off `n` and
/// starts `m` again, so that each of its calls makes `(n, m)` smaller as words
/// in a dictionary are, and no one of the parts alone goes down at each.
fn two_parts(measure: &str, inner: &str) -> String {
    format!(
        "use std.bits (u64)\nuse std.console (console, print)\n\nfn f(n: u64, m: u64) -> u64\n  decreases {measure}\n{{\n  if m > 0 {{\n    {inner}\n  }} else if n > 0 {{\n    f(n - 1, 5)\n  }} else {{\n    0\n  }}\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{f(3, 4)}}\")\n}}\n"
    )
}

fn a_decreases_of_two_parts_is_compared_as_words_in_a_dictionary_are() {
    let good = two_parts("n, m", "f(n, m - 1)");
    assert_eq!(refused(&good), Vec::<String>::new(), "for {good}");
    let lowered = lower(good.as_bytes()).unwrap();
    assert_eq!(scalar::run(&lowered).stdout, b"0\n".to_vec());
    assert!(!calls_itself(&scalar::passes(&lowered), "f"));
    // One part alone does not get smaller at the first call.
    let one_part = two_parts("m", "f(n, m - 1)");
    let errors = refused(&one_part);
    assert!(errors.iter().any(|e| e.contains("`m` is smaller at this call")), "{errors:?}");
    // The parts in the wrong order: `m` first, which goes up at the second.
    let wrong = two_parts("m, n", "f(n, m - 1)");
    let errors = refused(&wrong);
    assert!(errors.iter().any(|e| e.contains("`m, n` is smaller at this call")), "{errors:?}");
    // The first part stays and the second does not get smaller.
    let stuck = two_parts("n, m", "f(n, m)");
    let errors = refused(&stuck);
    assert!(errors.iter().any(|e| e.contains("`n, m` is smaller at this call")), "{errors:?}");
}

/// The program of the issue: `count(50000000, 0)` prints `50000000`, in an
/// executable whose stack does not grow with the number of calls, which a stack
/// of frames would not hold.
fn an_executable_of_a_function_that_calls_itself_fifty_million_times_prints_the_count_and_its_stack_does_not_grow() {
    let source = counting("\n  decreases n", TAIL).replace("count(5, 0)", "count(50000000, 0)");
    let said = Outcome {
        stdout: b"50000000\n".to_vec(),
        status: 0,
    };
    for target in runnable() {
        runs_as(source.as_bytes(), target, &said);
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
