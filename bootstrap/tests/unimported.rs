//! A name is in a file only when the file defines it, imports it, or the
//! prelude holds it. A name of the standard library that is none of these is
//! one error, at its first use, whose fix names the module that holds it, and
//! importing an effect brings the effect's name and nothing else.

use std::path::Path;
use std::sync::OnceLock;

use smith::diag::{Error, Pos};

use crate::Test;
use crate::programs::pos_after;

pub const ONCE: &[Test] = &[
    (
        "a_program_with_no_use_is_one_error_for_each_missing_name_at_its_first_use",
        a_program_with_no_use_is_one_error_for_each_missing_name_at_its_first_use,
    ),
    (
        "each_name_of_the_library_used_with_no_import_is_one_error_at_its_first_use_naming_its_module",
        each_name_of_the_library_used_with_no_import_is_one_error_at_its_first_use_naming_its_module,
    ),
    (
        "each_name_of_the_library_that_the_file_imports_is_not_refused",
        each_name_of_the_library_that_the_file_imports_is_not_refused,
    ),
    (
        "importing_an_effect_brings_its_name_alone",
        importing_an_effect_brings_its_name_alone,
    ),
    (
        "a_name_the_file_binds_defines_or_only_writes_as_a_field_is_not_refused",
        a_name_the_file_binds_defines_or_only_writes_as_a_field_is_not_refused,
    ),
    (
        "the_names_of_sizes_are_imported_and_int_byte_and_nat_are_not",
        the_names_of_sizes_are_imported_and_int_byte_and_nat_are_not,
    ),
    (
        "slice_is_in_every_file_and_freeze_is_imported_from_std_list",
        slice_is_in_every_file_and_freeze_is_imported_from_std_list,
    ),
];

/// A name outside the prelude and the module of `std` that holds it, written
/// as after `std.`.
type Held = (String, &'static str);

/// The names of the standard library outside the prelude, read from `std/`: the
/// items written with `pub`, the functions of the traits written with `pub`,
/// the effect `console`, and the names of sizes.
fn names() -> &'static [Held] {
    static NAMES: OnceLock<Vec<Held>> = OnceLock::new();
    NAMES.get_or_init(|| {
        let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../std");
        let mut modules: Vec<_> = std::fs::read_dir(root)
            .unwrap()
            .map(|e| e.unwrap().path())
            .filter(|p| p.extension().is_some_and(|e| e == "smith"))
            .filter(|p| p.file_stem().is_some_and(|s| s != "prelude"))
            .collect();
        modules.sort();
        let mut found: Vec<Held> = Vec::new();
        for path in modules {
            let module: &'static str =
                Box::leak(path.file_stem().unwrap().to_str().unwrap().to_string().into_boxed_str());
            let text = std::fs::read_to_string(&path).unwrap();
            let mut in_trait = false;
            for line in text.lines() {
                let word = |rest: &str| -> String {
                    rest.chars()
                        .take_while(|c| c.is_ascii_alphanumeric() || *c == '_')
                        .collect()
                };
                if let Some(rest) = line.strip_prefix("pub trait ") {
                    found.push((word(rest), module));
                    in_trait = true;
                } else if let Some(rest) = ["pub fn ", "pub struct ", "pub enum "]
                    .iter()
                    .find_map(|k| line.strip_prefix(k))
                {
                    found.push((word(rest), module));
                } else if in_trait && line == "}" {
                    in_trait = false;
                } else if in_trait && let Some(rest) = line.trim_start().strip_prefix("fn ") {
                    found.push((word(rest), module));
                }
            }
        }
        found.push(("console".to_string(), "console"));
        for size in ["u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64"] {
            found.push((size.to_string(), "bits"));
        }
        // A name that two modules export, as `boolean` of `Reader` and of
        // `Writer`, is told to come from the first of them.
        found.sort();
        found.dedup_by(|b, a| a.0 == b.0);
        found
    })
}

/// The errors that say a name is not imported.
fn missing(source: &str) -> Vec<Error> {
    match smith::check(source.as_bytes()) {
        Ok(_) => Vec::new(),
        Err(errors) => errors
            .into_iter()
            .filter(|e| e.message.contains("does not import it"))
            .collect(),
    }
}

fn mentions(e: &Error, name: &str, module: &str) -> bool {
    e.message.contains(&format!("`{name}`"))
        && e.message.contains(&format!("`std.{module}`"))
        && e.fix.as_deref().is_some_and(|fix| fix.contains(&format!("use std.{module} ({name})")))
}

/// `fn main() ! console { print("hi") }` with no `use` is refused with one
/// error for each name it lacks, at the name's first use.
fn a_program_with_no_use_is_one_error_for_each_missing_name_at_its_first_use() {
    let before_console = "fn main() ! ";
    let before_print = "fn main() ! console { ";
    let source = "fn main() ! console { print(\"hi\") }\n";
    let said = missing(source);
    assert_eq!(said.len(), 2, "{said:?}");
    assert_eq!(said[0].pos, pos_after(before_console));
    assert!(mentions(&said[0], "console", "console"), "{:?}", said[0]);
    assert_eq!(said[1].pos, pos_after(before_print));
    assert!(mentions(&said[1], "print", "console"), "{:?}", said[1]);
}

/// A way to write a name, and the text before its first use.
struct Shape {
    /// The program, with `NAME` where the name is written, twice.
    program: &'static str,
}

const SHAPES: &[Shape] = &[
    Shape {
        program: "fn main() {\n  x = NAME(1)\n  y = NAME(2)\n}\n",
    },
    Shape {
        program: "fn f(first: NAME, second: NAME) { }\n\nfn main() { }\n",
    },
    Shape {
        program: "fn main() ! alloc {\n  s = \"{NAME(1)} and {NAME(2)}\"\n}\n",
    },
];

/// Where the first `NAME` of the program is.
fn first_use(program: &str) -> Pos {
    pos_after(&program[..program.find("NAME").unwrap()])
}

/// Every name of the standard library outside the prelude, written in any of
/// the ways a name is written with no import, is one error at its first
/// use, however many times it is written, and its fix names the module.
fn each_name_of_the_library_used_with_no_import_is_one_error_at_its_first_use_naming_its_module() {
    for (name, module) in names() {
        for shape in SHAPES {
            let program = shape.program.replace("NAME", name);
            let said: Vec<Error> = missing(&program)
                .into_iter()
                .filter(|e| e.message.contains(&format!("`{name}`")))
                .collect();
            assert_eq!(said.len(), 1, "{name} in\n{program}\n{said:?}");
            assert_eq!(said[0].pos, first_use(shape.program), "{name}");
            assert!(mentions(&said[0], name, module), "{name}: {:?}", said[0]);
        }
    }
}

/// The same programs with the name imported are not refused for it.
fn each_name_of_the_library_that_the_file_imports_is_not_refused() {
    for (name, module) in names() {
        let program = SHAPES[0].program.replace("NAME", name);
        let source = format!("use std.{module} ({name})\n\n{program}");
        let said: Vec<Error> = missing(&source);
        assert!(said.is_empty(), "{name}: {said:?}\n{source}");
    }
}

/// Importing `console` brings the effect and not `print`, and importing
/// `print` brings the function and not the effect.
fn importing_an_effect_brings_its_name_alone() {
    let body = "fn main() ! console { print(\"hi\") }\n";
    let said = missing(&format!("use std.console (console)\n\n{body}"));
    assert_eq!(said.len(), 1, "{said:?}");
    assert!(mentions(&said[0], "print", "console"), "{:?}", said[0]);
    let said = missing(&format!("use std.console (print)\n\n{body}"));
    assert_eq!(said.len(), 1, "{said:?}");
    assert!(mentions(&said[0], "console", "console"), "{:?}", said[0]);
    let said = missing(&format!("use std.console (console, print)\n\n{body}"));
    assert!(said.is_empty(), "{said:?}");
}

/// A name the file defines, binds, or writes where a field or a named argument
/// is written is its own, so no import is asked for it.
fn a_name_the_file_binds_defines_or_only_writes_as_a_field_is_not_refused() {
    for (name, _) in names() {
        if name == "console" {
            continue;
        }
        let programs = [
            // Defined by the file.
            format!("fn {name}(x: Int) -> Int {{ x }}\n\nfn main() {{\n  y = {name}(1)\n}}\n"),
            // A parameter, and a binding.
            format!("fn f({name}: Int) -> Int {{ {name} }}\n\nfn main() {{\n  y = f(1)\n}}\n"),
            format!("fn main() {{\n  {name} = 1\n  y = {name}\n}}\n"),
            // A field, written and read, and a named argument.
            format!(
                "struct S {{\n  {name}: Int\n}}\n\nfn g({name}: Int) -> Int {{ {name} }}\n\n\
                 fn main() {{\n  s = S({name}: 1)\n  y = s.{name}\n  z = g({name}: 2)\n}}\n"
            ),
        ];
        for program in programs {
            let said = missing(&program);
            assert!(said.is_empty(), "{name}: {said:?}\n{program}");
        }
    }
}

/// `u32` and the other names of sizes are imported from `std.bits`, with the
/// fix saying so, and `Int`, `Byte`, and `Nat` are in every file.
fn the_names_of_sizes_are_imported_and_int_byte_and_nat_are_not() {
    for size in ["u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64"] {
        let program = format!("fn f(x: {size}) -> {size} {{ x }}\n\nfn main() {{ }}\n");
        let said = missing(&program);
        assert_eq!(said.len(), 1, "{size}: {said:?}");
        assert!(mentions(&said[0], size, "bits"), "{size}: {:?}", said[0]);
        let said = missing(&format!("use std.bits ({size})\n\n{program}"));
        assert!(said.is_empty(), "{size}: {said:?}");
    }
    let program = "fn f(a: Int, b: Byte, c: Nat) -> Int { a }\n\nfn main() { }\n";
    assert!(missing(program).is_empty());
}

/// `Slice` is in the prelude, as a piece of a list written `x[a..b]` means it,
/// so a file writes it with no import and is refused for importing it, and
/// `freeze`, which no syntax means, is imported from `std.list`.
fn slice_is_in_every_file_and_freeze_is_imported_from_std_list() {
    let program = "fn f(s: Slice[Byte]) -> Nat { s.size }\n\nfn main() { }\n";
    assert!(missing(program).is_empty());
    let said = match smith::check(b"use std.list (Slice)\n\nfn main() { }\n") {
        Ok(_) => String::new(),
        Err(e) => format!("{e:?}"),
    };
    assert!(said.contains("is in the prelude"), "{said}");
    let call = "fn f(xs: take List[Byte]) -> Slice[Byte] { freeze(xs) }\n\nfn main() { }\n";
    let said = missing(call);
    assert_eq!(said.len(), 1, "{said:?}");
    assert!(mentions(&said[0], "freeze", "list"), "{:?}", said[0]);
    assert!(missing(&format!("use std.list (freeze)\n\n{call}")).is_empty());
}
