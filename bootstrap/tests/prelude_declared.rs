//! A program reads the prelude's traits of numbers only when it calls for one,
//! by writing the name of the trait or of one of its functions, or by importing
//! a module that does, so that a program pays nothing for a line of them that
//! it does not use.

use smith::prelude_numeric_read;

use crate::Test;

pub const ONCE: &[Test] = &[
    (
        "a_program_that_names_no_trait_of_numbers_reads_none",
        a_program_that_names_no_trait_of_numbers_reads_none,
    ),
    (
        "a_program_reads_the_trait_of_numbers_it_names_or_one_of_its_functions_and_no_other",
        a_program_reads_the_trait_of_numbers_it_names_or_one_of_its_functions_and_no_other,
    ),
    (
        "a_program_reads_the_traits_of_numbers_that_a_module_it_imports_calls_for",
        a_program_reads_the_traits_of_numbers_that_a_module_it_imports_calls_for,
    ),
];

fn read(source: &str) -> Vec<String> {
    let mut found = prelude_numeric_read(source.as_bytes())
        .unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
    found.sort();
    found
}

fn a_program_that_names_no_trait_of_numbers_reads_none() {
    let source = "use std.console (console, print)\n\nfn main() ! console, alloc {\n  print(\"hello\")\n}\n";
    assert_eq!(read(source), Vec::<String>::new());
    let source = "use std.console (console, print)\n\nfn main() ! console, alloc {\n  x = 3 + 4 * 2\n  print(\"{x}\")\n}\n";
    assert_eq!(read(source), Vec::<String>::new());
}

fn a_program_reads_the_trait_of_numbers_it_names_or_one_of_its_functions_and_no_other() {
    let wrapping = "use std.console (console, print)\n\nfn main() ! console, alloc {\n  print(\"{Wrapping.wrapping_mul(200, 2)}\")\n}\n";
    assert_eq!(read(wrapping), ["Wrapping"]);
    let checked = "use std.console (console, print)\n\nfn main() ! console, alloc {\n  a: Byte = 200\n  print(\"{Checked.checked_add(a, 1)}\")\n}\n";
    assert_eq!(read(checked), ["Checked"]);
}

fn a_program_reads_the_traits_of_numbers_that_a_module_it_imports_calls_for() {
    let source = "use std.console (console, print)\nuse std.int (wrapping_add)\n\nfn main() ! console, alloc {\n  a: Byte = 200\n  print(\"{wrapping_add(a, 100)}\")\n}\n";
    let found = read(source);
    assert!(!found.is_empty(), "{found:?}");
}
