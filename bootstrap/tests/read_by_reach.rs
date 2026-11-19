//! A program pays, for a module of the library it imports, for the items it
//! reaches and not for the others. A module is built with 1, 10, and 100 times
//! as many functions, types, and `impl`s that nothing reaches, and the program
//! that imports it builds the same program, allocates no more, and makes as
//! many comparisons of definitions and of copies of functions. A mistake in an
//! item nothing reaches is not found by a program that imports the module, and
//! is found by the check of the whole library, which finds none in `std/`.

use smith::work::compared;

use crate::Test;
use crate::counting::allocated;

pub const ONCE: &[Test] = &[
    (
        "a_module_costs_what_the_program_reaches_of_it_and_not_what_it_holds",
        a_module_costs_what_the_program_reaches_of_it_and_not_what_it_holds,
    ),
    (
        "a_mistake_in_an_item_no_program_reaches_is_found_by_the_check_of_the_whole_library",
        a_mistake_in_an_item_no_program_reaches_is_found_by_the_check_of_the_whole_library,
    ),
    (
        "every_module_of_the_standard_library_checks_whole",
        every_module_of_the_standard_library_checks_whole,
    ),
];

/// How many items of each kind are added to the module, for each size.
const COPIES: [usize; 3] = [1, 10, 100];

/// The most that the largest module may allocate beyond the smallest, for the
/// lists that grow by doubling.
const SLACK: u64 = 24;

/// The module: what the program reaches, a function that calls an `impl`, and
/// then `copies` of a struct, an enum, a function, and an `impl` of the trait
/// for the struct, which nothing reaches.
fn module(copies: usize) -> String {
    let mut text = String::from(
        "pub struct Square {
  side: Int
}

pub trait Area[T] {
  fn area(x: T) -> Int
}

impl Area[Square] {
  fn area(x: Square) -> Int {
    x.side
  }
}

pub fn total(x: Int) -> Int {
  area(Square(side: x))
}
",
    );
    for n in 0..copies {
        text += &format!(
            "
struct Shape{n} {{
  a: Int
}}

enum Kind{n} {{
  one
  two: Int
}}

fn helper{n}(x: Int) -> Int {{
  x
}}

impl Area[Shape{n}] {{
  fn area(x: Shape{n}) -> Int {{
    helper{n}(x.a)
  }}
}}
"
        );
    }
    text
}

const PROGRAM: &str = "use std.console (console, print)
use std.m (total)

fn main() ! console, alloc {
  print(\"{total(3)}\")
}
";

fn a_module_costs_what_the_program_reaches_of_it_and_not_what_it_holds() {
    // What the process reads once and keeps is read before anything is counted.
    let warm = module(1);
    let console = smith::library_text("console");
    smith::lower_in(PROGRAM.as_bytes(), &[("m", warm.as_str()), ("console", console)])
        .unwrap_or_else(|e| panic!("{e:?}"));
    let mut built = Vec::new();
    for copies in COPIES {
        let text = module(copies);
        let console = smith::library_text("console");
        let library = [("m", text.as_str()), ("console", console)];
        let before = compared();
        let (program, front) = allocated(|| smith::lower_in(PROGRAM.as_bytes(), &library));
        let after = compared();
        let program = program.unwrap_or_else(|e| panic!("{e:?}"));
        built.push((
            program,
            front,
            after.items - before.items,
            after.instances - before.instances,
        ));
    }
    let (first, rest) = built.split_first().unwrap();
    for (program, front, items, instances) in rest {
        assert!(*program == first.0, "the program is another one");
        assert!(
            front.calls <= first.1.calls + SLACK,
            "reading allocates {} times for {} copies and {} times for {}",
            front.calls,
            COPIES[2],
            first.1.calls,
            COPIES[0]
        );
        assert_eq!((*items, *instances), (first.2, first.3));
    }
}

/// A function that does not type check, which a program never calls.
fn module_with_a_mistake() -> (String, usize) {
    let mut text = module(3);
    let line = text.lines().count() + 2;
    text += "\nfn broken(x: Int) -> Int {\n  true\n}\n";
    (text, line)
}

fn a_mistake_in_an_item_no_program_reaches_is_found_by_the_check_of_the_whole_library() {
    let (text, line) = module_with_a_mistake();
    let console = smith::library_text("console");
    let library = [("m", text.as_str()), ("console", console)];
    assert!(smith::lower_in(PROGRAM.as_bytes(), &library).is_ok());
    let errors = smith::check_library(&library);
    assert!(
        errors.iter().any(|e| e.pos.line == line + 1),
        "the mistake in line {} is not among {errors:?}",
        line + 1
    );
    assert!(smith::check_library(&[("m", module(3).as_str()), ("console", console)]).is_empty());
}

fn every_module_of_the_standard_library_checks_whole() {
    let errors = smith::check_standard_library();
    assert!(errors.is_empty(), "{errors:?}");
}
