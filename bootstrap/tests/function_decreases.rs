//! `decreases` on a function, and what a function that calls itself is
//! asked: it calls itself last, as the last expression of its body or of a
//! block of an `if` or a `match` that is in that place, or as what a `return`
//! gives, and it has a `decreases` that `arith` proves is not below zero and
//! smaller at each of the calls. Such a function is a loop in the scalar form.
//! A call that is not the last thing a function does is held to the same rule,
//! and keeps a frame, so the function carries `alloc`. Functions that call each
//! other in a circle each have a `decreases` of the same number of parts, and a
//! call from one to another is held to the measure the callee states.

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
        "a_call_of_itself_that_is_not_the_last_thing_a_function_does_gives_what_the_calls_gave_to_any_depth",
        a_call_of_itself_that_is_not_the_last_thing_a_function_does_gives_what_the_calls_gave_to_any_depth,
    ),
    (
        "a_call_of_itself_that_is_not_the_last_thing_a_function_does_needs_alloc_and_a_measure_proven_smaller",
        a_call_of_itself_that_is_not_the_last_thing_a_function_does_needs_alloc_and_a_measure_proven_smaller,
    ),
    (
        "functions_that_call_each_other_give_what_the_calls_gave_to_any_depth_with_a_measure_of_one_part_or_three",
        functions_that_call_each_other_give_what_the_calls_gave_to_any_depth_with_a_measure_of_one_part_or_three,
    ),
    (
        "a_circle_of_functions_needs_a_decreases_of_the_same_parts_in_each_and_a_call_proven_smaller_and_alloc_where_a_frame_is_kept",
        a_circle_of_functions_needs_a_decreases_of_the_same_parts_in_each_and_a_call_proven_smaller_and_alloc_where_a_frame_is_kept,
    ),
];

pub const MACHINE: &[Test] = &[
    (
        "an_executable_of_a_function_that_calls_itself_fifty_million_times_prints_the_count_and_its_stack_does_not_grow",
        an_executable_of_a_function_that_calls_itself_fifty_million_times_prints_the_count_and_its_stack_does_not_grow,
    ),
    (
        "an_executable_of_a_function_that_calls_itself_a_million_deep_not_last_prints_the_depth",
        an_executable_of_a_function_that_calls_itself_a_million_deep_not_last_prints_the_depth,
    ),
    (
        "an_executable_of_functions_that_call_each_other_a_million_deep_prints_the_sum_and_one_that_calls_last_needs_no_alloc",
        an_executable_of_functions_that_call_each_other_a_million_deep_prints_the_sum_and_one_that_calls_last_needs_no_alloc,
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

/// A program of `depth`, which adds one after each call of itself, and a
/// `main` that prints it at each of `at`.
fn depth(body: &str, header: &str, at: &[u64]) -> String {
    let prints: String = at.iter().map(|n| format!("  print(\"{{depth({n})}}\")\n")).collect();
    format!(
        "use std.bits (u64)\nuse std.console (console, print)\nuse std.int (wrapping_add)\n\nfn depth(n: u64) -> u64{header}\n  decreases n\n{{\n  {body}\n}}\n\nfn main() ! console, alloc {{\n{prints}}}\n"
    )
}

const NOT_LAST: &str = "if n == 0 { 0 } else { wrapping_add(depth(n - 1), 1) }";

fn a_call_of_itself_that_is_not_the_last_thing_a_function_does_gives_what_the_calls_gave_to_any_depth() {
    for body in [
        NOT_LAST,
        "if n == 0 { 0 } else { x = depth(n - 1)\n  wrapping_add(x, 1) }",
        "if n == 0 { 0 } else { wrapping_add(1, depth(n - 1)) }",
    ] {
        for n in [0u64, 1, 2, 7, 100, 10_000] {
            let source = depth(body, " ! alloc", &[n]);
            assert_eq!(refused(&source), Vec::<String>::new(), "for {source}");
            let lowered = lower(source.as_bytes()).unwrap();
            let said = scalar::run(&lowered);
            assert_eq!(said.stdout, format!("{n}\n").into_bytes(), "{source}");
            assert_eq!(scalar::run(&scalar::passes(&lowered)), said, "{source}");
        }
    }
}

/// The program of the issue: `depth(3)` and `depth(1000000)`, in an executable
/// whose stack is the system's at its usual size.
fn an_executable_of_a_function_that_calls_itself_a_million_deep_not_last_prints_the_depth() {
    let source = depth(NOT_LAST, " ! alloc", &[3, 10_000_000]);
    let said = Outcome {
        stdout: b"3\n10000000\n".to_vec(),
        status: 0,
    };
    for target in runnable() {
        runs_as(source.as_bytes(), target, &said);
    }
}

fn a_call_of_itself_that_is_not_the_last_thing_a_function_does_needs_alloc_and_a_measure_proven_smaller() {
    // Without `alloc` it is refused as any missing effect is.
    let errors = refused(&depth(NOT_LAST, "", &[3]));
    assert!(
        errors.iter().any(|e| e.contains("needs the `alloc` effect")),
        "{errors:?}"
    );
    // A call that is last needs nothing.
    let last = counting("\n  decreases n", TAIL);
    assert_eq!(refused(&last), Vec::<String>::new());
    // A measure that is not smaller is refused with the goal, as at a call that is last.
    let stuck = depth("if n == 0 { 0 } else { wrapping_add(depth(n), 1) }", " ! alloc", &[3]);
    let errors = refused(&stuck);
    assert!(errors.iter().any(|e| e.contains("`n` is smaller at this call")), "{errors:?}");
    // And a function with no `decreases` is asked for one.
    let bare = depth(NOT_LAST, " ! alloc", &[3]).replace("\n  decreases n", "");
    let errors = refused(&bare);
    assert!(errors.iter().any(|e| e.contains("needs a `decreases`")), "{errors:?}");
}

/// A program of `even` and `odd`, which call each other and add 1 and 2, with
/// `header` after both signatures and a `decreases` of `even_measure` and
/// `odd_measure`, and a `main` that prints `even` at each of `at`.
fn ring(header: &str, even_measure: &str, odd_measure: &str, at: &[u64]) -> String {
    let prints: String = at.iter().map(|n| format!("  print(\"{{even({n})}}\")\n")).collect();
    format!(
        "use std.bits (u64)\nuse std.console (console, print)\nuse std.int (wrapping_add)\n\nfn even(n: u64) -> u64{header}{even_measure}\n{{\n  if n == 0 {{ 0 }} else {{ wrapping_add(odd(n - 1), 1) }}\n}}\n\nfn odd(n: u64) -> u64{header}{odd_measure}\n{{\n  if n == 0 {{ 0 }} else {{ wrapping_add(even(n - 1), 2) }}\n}}\n\nfn main() ! console, alloc {{\n{prints}}}\n"
    )
}

/// What `even(n)` gives: 1 for each of the calls at an even distance, and 2 for
/// the others.
fn ring_gives(n: u64) -> u64 {
    n / 2 * 3 + n % 2
}

/// A circle of three functions with a measure of two parts, as `sum`,
/// `product`, and `atom` of the page are: the first part stays and the second
/// goes from 2 to 1 to 0, and then the first gets smaller.
fn parts(n: u64) -> String {
    format!(
        "use std.bits (u64)\nuse std.console (console, print)\nuse std.int (wrapping_add)\n\nfn sum(n: u64) -> u64 ! alloc\n  decreases n, 2\n{{\n  if n == 0 {{ 0 }} else {{ wrapping_add(product(n), 1) }}\n}}\n\nfn product(n: u64) -> u64 ! alloc\n  decreases n, 1\n{{\n  wrapping_add(atom(n), 1)\n}}\n\nfn atom(n: u64) -> u64 ! alloc\n  decreases n, 0\n{{\n  if n == 0 {{ 0 }} else {{ wrapping_add(sum(n - 1), 1) }}\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{sum({n})}}\")\n}}\n"
    )
}

fn functions_that_call_each_other_give_what_the_calls_gave_to_any_depth_with_a_measure_of_one_part_or_three() {
    for n in [0u64, 1, 2, 7, 100, 10_000] {
        let source = ring(" ! alloc", "\n  decreases n", "\n  decreases n", &[n]);
        assert_eq!(refused(&source), Vec::<String>::new(), "for {source}");
        let lowered = lower(source.as_bytes()).unwrap();
        let said = scalar::run(&lowered);
        assert_eq!(said.stdout, format!("{}\n", ring_gives(n)).into_bytes(), "{source}");
        assert_eq!(scalar::run(&scalar::passes(&lowered)), said, "{source}");
        // Three functions, of a measure of two parts: each call is 1 more.
        let source = parts(n);
        assert_eq!(refused(&source), Vec::<String>::new(), "for {source}");
        let said = scalar::run(&lower(source.as_bytes()).unwrap());
        assert_eq!(said.stdout, format!("{}\n", 3 * n).into_bytes(), "{source}");
    }
}

fn an_executable_of_functions_that_call_each_other_a_million_deep_prints_the_sum_and_one_that_calls_last_needs_no_alloc() {
    let source = ring(" ! alloc", "\n  decreases n", "\n  decreases n", &[3, 1_000_000]);
    let said = Outcome {
        stdout: format!("{}\n{}\n", ring_gives(3), ring_gives(1_000_000)).into_bytes(),
        status: 0,
    };
    for target in runnable() {
        runs_as(source.as_bytes(), target, &said);
    }
    // Two functions that call each other last are loops, and carry nothing.
    let last = "use std.bits (u64)\nuse std.console (console, print)\n\nfn even(n: u64) -> u64\n  decreases n\n{\n  if n == 0 { 0 } else { odd(n - 1) }\n}\n\nfn odd(n: u64) -> u64\n  decreases n\n{\n  if n == 0 { 1 } else { even(n - 1) }\n}\n\nfn main() ! console, alloc {\n  print(\"{even(5000000)}\")\n  print(\"{odd(5000000)}\")\n}\n";
    assert_eq!(refused(last), Vec::<String>::new());
    let said = Outcome {
        stdout: b"0\n1\n".to_vec(),
        status: 0,
    };
    for target in runnable() {
        runs_as(last.as_bytes(), target, &said);
    }
}

fn a_circle_of_functions_needs_a_decreases_of_the_same_parts_in_each_and_a_call_proven_smaller_and_alloc_where_a_frame_is_kept() {
    let said = |errors: &[String], what: &str| {
        assert!(errors.iter().any(|e| e.contains(what)), "{errors:?} lacks {what}");
    };
    // A function of the circle with no `decreases` is refused, and the error names the circle.
    let errors = refused(&ring(" ! alloc", "\n  decreases n", "", &[3]));
    said(&errors, "`even` and `odd` call each other");
    said(&errors, "which `odd` has not");
    // Of more functions, and two with none.
    let bare = parts(3).replace("\n  decreases n, 2", "").replace("\n  decreases n, 1", "");
    let errors = refused(&bare);
    said(&errors, "`sum`, `product`, and `atom` call each other");
    said(&errors, "which `sum` and `product` have none");
    // Measures of different numbers of parts.
    let errors = refused(&ring(" ! alloc", "\n  decreases n", "\n  decreases n, 0", &[3]));
    said(&errors, "the same number of parts");
    said(&errors, "`even` has 1, `odd` has 2");
    // A call that does not make the measure smaller is refused where it is, with the goal.
    let stuck = ring(" ! alloc", "\n  decreases n", "\n  decreases n", &[3])
        .replace("wrapping_add(odd(n - 1), 1)", "wrapping_add(odd(n), 1)");
    said(&refused(&stuck), "`n` is smaller at this call");
    // A circle that keeps a frame needs `alloc` in each function of it.
    let errors = refused(&ring("", "\n  decreases n", "\n  decreases n", &[3]));
    said(&errors, "needs the `alloc` effect, which `even` does not declare");
    said(&errors, "needs the `alloc` effect, which `odd` does not declare");
}
