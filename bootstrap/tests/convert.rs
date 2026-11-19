//! Properties of `convert` that hold for every pair of integer types: when it
//! compiles, and what it gives. What it gives for the values a generator
//! draws is in the generator of `numbers`.

use smith::check;

use crate::Test;
use crate::numbers::{INTS, header_for};

pub const ONCE: &[Test] = &[
    (
        "convert_between_integer_types_compiles_exactly_when_the_second_holds_every_value_of_the_first",
        convert_between_integer_types_compiles_exactly_when_the_second_holds_every_value_of_the_first,
    ),
    (
        "a_conversion_of_a_conversion_is_refused_for_every_type",
        a_conversion_of_a_conversion_is_refused_for_every_type,
    ),
];

/// A program with a function that takes an `x` of type `from` and gives what
/// `body` says, of type `to`.
fn program(from: &str, to: &str, body: &str) -> String {
    let program = format!(
        "fn f(x: {from}) -> {to} {{\n  {body}\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{f(1)}}\")\n}}\n"
    );
    header_for(&program) + &program
}

/// `convert` from one integer type to another compiles when the second holds
/// every value of the first and the two differ, and not otherwise.
fn convert_between_integer_types_compiles_exactly_when_the_second_holds_every_value_of_the_first() {
    for from in INTS {
        for to in INTS {
            let source = program(from.name, to.name, "convert(x)");
            assert_eq!(
                check(source.as_bytes()).is_ok(),
                from != to && to.holds_all(*from),
                "convert from {} to {}",
                from.name,
                to.name
            );
        }
    }
}

/// Where no type is expected, `convert` is refused and the error says so, and
/// a conversion that would need two steps is refused too.
fn a_conversion_of_a_conversion_is_refused_for_every_type() {
    for from in INTS {
        for to in INTS.iter().filter(|t| t.holds_all(*from) && *t != from) {
            let source = program(from.name, to.name, "convert(convert(x))");
            let errors = check(source.as_bytes()).expect_err("an error");
            assert!(
                errors
                    .iter()
                    .any(|e| e.message.contains("expected") && e.message.contains("convert")),
                "convert of convert from {} to {}",
                from.name,
                to.name
            );
        }
    }
}
