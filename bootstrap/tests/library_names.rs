//! A name a program lists from a module means that name in the program, and
//! never changes what a name means in another module: a function of one module
//! and a function of a trait of another module that have one name both stand,
//! as they do when the program lists neither, and a parameter that is named
//! like a function its module has is the parameter where it is written.

use smith::lower_project;
use smith::scalar;

use crate::Test;

pub const ONCE: &[Test] = &[
    (
        "a_function_the_program_lists_and_a_function_of_a_trait_of_the_library_with_one_name_both_stand",
        a_function_the_program_lists_and_a_function_of_a_trait_of_the_library_with_one_name_both_stand,
    ),
    (
        "a_function_the_program_lists_of_one_module_is_not_what_another_module_calls_by_that_name",
        a_function_the_program_lists_of_one_module_is_not_what_another_module_calls_by_that_name,
    ),
    (
        "a_parameter_named_like_a_function_of_its_module_is_the_parameter_where_it_is_written",
        a_parameter_named_like_a_function_of_its_module_is_the_parameter_where_it_is_written,
    ),
];

/// What `main` prints when it is built with the modules.
fn printed(main: &str, modules: &[(&str, &str)]) -> String {
    let program = lower_project(main.as_bytes(), modules)
        .unwrap_or_else(|e| panic!("{e:?} for\n{main}\nand\n{modules:?}"));
    String::from_utf8(scalar::run(&program).stdout).unwrap()
}

/// `std.jsonrpc` has a function `read`, and `std.deserialize` has the trait
/// `Deserialize`, whose function is `read` too. A program that lists the one
/// and calls it does not make the other the same function.
fn a_function_the_program_lists_and_a_function_of_a_trait_of_the_library_with_one_name_both_stand() {
    let main = "\
use std.console (console, print)
use std.jsonrpc (read)

fn main() ! console, alloc {
  match read(\"1\") {
    .ok(r) { print(\"read\") }
    .err(e) { print(\"refused\") }
  }
}
";
    assert_eq!(printed(main, &[]), "refused\n");
}

/// A name the program lists from `std.der` is not what `std.json`, which reads
/// the functions of the traits of `std.deserialize`, calls `text`.
fn a_function_the_program_lists_of_one_module_is_not_what_another_module_calls_by_that_name() {
    let main = "\
use std.console (console, print)
use std.der (text)
use std.json (raw_json, raw_text)

fn main() ! console, alloc {
  match raw_json(\"[1, 2]\") {
    .ok(r) { print(raw_text(r)) }
    .err(e) { print(\"refused\") }
  }
}
";
    assert_eq!(printed(main, &[]), "[1, 2]\n");
}

/// A parameter that is named like a function its module has, and an argument
/// given by that name, are that parameter where they are written.
fn a_parameter_named_like_a_function_of_its_module_is_the_parameter_where_it_is_written() {
    let shapes = "\
pub fn body() -> Nat { 3 }

fn wrap(body: Nat) -> Nat { body }

pub fn render(body: Nat) -> Nat { wrap(body: body) }
";
    let main = "\
use std.console (console, print)
use shapes (render)

fn main() ! console, alloc {
  print(\"{render(5)}\")
}
";
    assert_eq!(printed(main, &[("shapes", shapes)]), "5\n");
}
