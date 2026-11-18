//! `use` for the names of the standard library: what a file that imports the
//! names it uses builds, what is said of a `use` that is wrong or not needed,
//! and that the compiler needs nothing beside its executable to read the
//! library. The modules and their names are read from `std/`, so a module
//! that is added is covered by every property here.

use std::collections::HashSet;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::OnceLock;

use smith::diag::Pos;
use smith::{check, compile, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::programs::pos_after;
use crate::targets::any;
use crate::{numbers, programs, text};

pub const ONCE: &[Test] = &[
    (
        "each_name_the_compiler_supplies_is_in_the_module_the_library_pages_name",
        each_name_the_compiler_supplies_is_in_the_module_the_library_pages_name,
    ),
    (
        "every_name_of_a_module_can_be_imported_at_once",
        every_name_of_a_module_can_be_imported_at_once,
    ),
    (
        "a_file_that_imports_the_names_it_uses_builds_what_it_built_without_the_use",
        a_file_that_imports_the_names_it_uses_builds_what_it_built_without_the_use,
    ),
    (
        "a_name_that_is_imported_and_not_used_is_a_warning_at_its_name_and_changes_nothing",
        a_name_that_is_imported_and_not_used_is_a_warning_at_its_name_and_changes_nothing,
    ),
    (
        "each_mistake_in_a_use_is_one_error_at_its_place",
        each_mistake_in_a_use_is_one_error_at_its_place,
    ),
    (
        "a_use_after_another_item_is_one_error_at_the_use",
        a_use_after_another_item_is_one_error_at_the_use,
    ),
    (
        "a_build_needs_no_file_beside_the_executable_to_import_from_std",
        a_build_needs_no_file_beside_the_executable_to_import_from_std,
    ),
];

/// The tests whose result depends on the kind of machine they run on.
pub const MACHINE: &[Test] = &[(
    "a_run_needs_no_file_beside_the_executable_to_import_from_std",
    a_run_needs_no_file_beside_the_executable_to_import_from_std,
)];

/// A module of the standard library: its name, and the names it exports.
struct Module {
    name: String,
    /// Each exported name, and whether it is a function.
    names: Vec<(String, bool)>,
}

/// Every module in `std/`, with what its source writes with `pub`, and for
/// `std.console` the effect `console`, which the compiler holds.
fn library() -> &'static [Module] {
    static LIBRARY: OnceLock<Vec<Module>> = OnceLock::new();
    LIBRARY.get_or_init(|| {
        let dir = Path::new(env!("CARGO_MANIFEST_DIR")).join("../std");
        let mut files: Vec<PathBuf> = std::fs::read_dir(dir)
            .unwrap()
            .map(|e| e.unwrap().path())
            .filter(|p| p.extension().is_some_and(|e| e == "smith"))
            .collect();
        files.sort();
        files
            .iter()
            .map(|file| {
                let name = file.file_stem().unwrap().to_str().unwrap().to_string();
                let mut names = Vec::new();
                for line in std::fs::read_to_string(file).unwrap().lines() {
                    for (start, is_fn) in [
                        ("pub fn ", true),
                        ("pub struct ", false),
                        ("pub trait ", false),
                        ("pub enum ", false),
                    ] {
                        if let Some(rest) = line.strip_prefix(start) {
                            let written: String = rest
                                .chars()
                                .take_while(|c| c.is_alphanumeric() || *c == '_')
                                .collect();
                            names.push((written, is_fn));
                        }
                    }
                }
                if name == "console" {
                    names.push(("console".to_string(), false));
                }
                Module { name, names }
            })
            .collect()
    })
}

/// The module that exports the name.
fn home(name: &str) -> Option<&'static str> {
    library()
        .iter()
        .find(|m| m.names.iter().any(|(n, _)| n == name))
        .map(|m| m.name.as_str())
}

const HELLO: &str = "fn main() ! console {\n  print(\"hi\")\n}\n";

/// The names the compiler supplies outside the prelude, by the module the
/// pages of `knowledge/library/` name for each.
fn supplied() -> Vec<(&'static str, Vec<String>)> {
    let family = |stems: &[&str], ops: &[&str]| -> Vec<String> {
        stems
            .iter()
            .flat_map(|s| ops.iter().map(move |o| format!("{s}_{o}")))
            .collect()
    };
    let mut int = family(&["wrapping", "saturating", "checked"], &["add", "sub", "mul"]);
    int.extend(family(&["wrapping", "saturating", "checked"], &["narrow"]));
    int.push("narrow".to_string());
    let words = |names: &[&str]| names.iter().map(|n| n.to_string()).collect();
    vec![
        ("console", words(&["console", "print"])),
        ("list", words(&["push"])),
        ("utf8", words(&["utf8", "Utf8Error"])),
        ("int", int),
        (
            "bits",
            words(&[
                "Bits",
                "bit_and",
                "bit_or",
                "bit_xor",
                "bit_not",
                "shift_left",
                "shift_right",
            ]),
        ),
    ]
}

/// Each name the compiler supplies has a module that can be imported from.
fn each_name_the_compiler_supplies_is_in_the_module_the_library_pages_name() {
    for (module, names) in supplied() {
        for name in names {
            assert_eq!(home(&name), Some(module), "the module of `{name}`");
        }
    }
}

/// A file that lists every name of a module, once each, is not refused.
fn every_name_of_a_module_can_be_imported_at_once() {
    for module in library() {
        let names: Vec<&str> = module.names.iter().map(|(n, _)| n.as_str()).collect();
        let source = format!("use std.{} ({})\n\n{HELLO}", module.name, names.join(", "));
        if let Err(errors) = lower(source.as_bytes()) {
            panic!("{errors:?} for {source}");
        }
    }
}

/// The names a source writes, by the lexer, and those it defines.
fn names_of(source: &str) -> (HashSet<String>, HashSet<String>) {
    let mut written = HashSet::new();
    for line in smith::lex_plain(source.as_bytes()).unwrap().lines() {
        let parts: Vec<&str> = line.split(' ').collect();
        if parts[0] == "ident" {
            let bytes: Vec<u8> = parts[3].split(',').map(|b| b.parse().unwrap()).collect();
            written.insert(String::from_utf8(bytes).unwrap());
        }
    }
    let mut defined = HashSet::new();
    for line in source.lines() {
        let line = line.trim_start();
        for word in ["fn ", "struct ", "enum ", "trait "] {
            if let Some(rest) = line.strip_prefix(word) {
                let name: String = rest
                    .chars()
                    .take_while(|c| c.is_alphanumeric() || *c == '_')
                    .collect();
                defined.insert(name);
            }
        }
    }
    (written, defined)
}

/// The source with a `use` line, at its top, for each module whose names it
/// writes, listing exactly those names, in the order the modules are in.
fn with_imports(source: &str) -> String {
    let (written, defined) = names_of(source);
    let mut lines = String::new();
    for module in library() {
        let used: Vec<&str> = (module.names.iter())
            .map(|(n, _)| n.as_str())
            .filter(|n| written.contains(*n) && !defined.contains(*n))
            .collect();
        if !used.is_empty() {
            lines.push_str(&format!("use std.{} ({})\n", module.name, used.join(", ")));
        }
    }
    if lines.is_empty() {
        return source.to_string();
    }
    format!("{lines}\n{source}")
}

/// A program the generators build that has no mistake.
fn some_program(g: &mut Gen) -> String {
    match g.below(3) {
        0 => numbers::program(g).source,
        1 => text::program(g).source,
        _ => String::from_utf8(programs::program(g).source).unwrap(),
    }
}

/// A file that lists the names it uses, and only those, is built as the
/// same executable, with the same warnings, as the file with no `use`,
/// because every name of the standard library is in scope until a name
/// without its `use` is an error.
fn a_file_that_imports_the_names_it_uses_builds_what_it_built_without_the_use() {
    generate::check(300, 50, some_program, |source| {
        let with = with_imports(source);
        assert_ne!(&with, source, "a program that prints imports `print`");
        assert_eq!(
            compile(with.as_bytes(), any()).map_err(|e| format!("{e:?}")),
            compile(source.as_bytes(), any()).map_err(|e| format!("{e:?}")),
            "for\n{with}"
        );
        let warnings = |text: &str| {
            check(text.as_bytes())
                .map(|w| w.into_iter().map(|w| w.message).collect::<Vec<_>>())
                .map_err(|e| format!("{e:?}"))
        };
        assert_eq!(warnings(&with), warnings(source), "for\n{with}");
    });
}

/// A name that is listed and not written anywhere else is a warning at the
/// name, and not an error, and the executable is the one without it.
fn a_name_that_is_imported_and_not_used_is_a_warning_at_its_name_and_changes_nothing() {
    for module in library() {
        for (name, _) in &module.names {
            let bare = if name == "console" || name == "print" {
                "fn main() {\n}\n"
            } else {
                HELLO
            };
            let head = format!("use std.{} (", module.name);
            let source = format!("{head}{name})\n\n{bare}");
            let warnings = check(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
            let places: Vec<Pos> = warnings.iter().map(|w| w.pos).collect();
            let at = Pos {
                line: 1,
                col: head.chars().count() + 1,
            };
            assert_eq!(places, [at], "{warnings:?} for {source}");
            assert!(warnings[0].message.contains(name.as_str()), "{warnings:?}");
            assert_eq!(
                compile(source.as_bytes(), any()),
                compile(bare.as_bytes(), any()),
                "for {source}"
            );
        }
    }
}

/// A file of `use` lines with mistakes, and the error each must give: where
/// it is, and what it must mention.
#[derive(Debug)]
struct Uses {
    source: String,
    expected: Vec<(Pos, String)>,
    /// What the file defines after its `main`.
    defined: Vec<String>,
}

impl Uses {
    fn push(&mut self, text: &str) {
        self.source.push_str(text);
    }

    /// Writes `text`, which holds a mistake that is at its start.
    fn mistake(&mut self, text: &str, what: impl Into<String>) {
        self.expected.push((pos_after(&self.source), what.into()));
        self.push(text);
    }
}

/// A file with a `use` for up to three modules, each right or wrong in one
/// way, and a `main`.
fn uses(g: &mut Gen) -> Uses {
    let mut file = Uses {
        source: String::new(),
        expected: Vec::new(),
        defined: Vec::new(),
    };
    let mut modules: Vec<&Module> = library().iter().collect();
    for _ in 0..1 + g.below(3) {
        if modules.is_empty() {
            break;
        }
        let module = modules.remove(g.below(modules.len()));
        let names: Vec<&str> = module.names.iter().map(|(n, _)| n.as_str()).collect();
        let first = *g.pick(&names);
        let m = format!("std.{}", module.name);
        // A function that a file may define, as it is no function of a trait.
        let definable: Vec<&str> = names
            .iter()
            .copied()
            .filter(|n| ["push", "utf8", "narrow"].contains(n) || n.ends_with("_narrow"))
            .collect();
        match g.below(7) {
            0 => file.push(&format!("use {m} ({first})\n")),
            1 => {
                let bad = *g.pick(&["std.nope", "std.fs", "other.thing"]);
                file.push("use ");
                file.mistake(bad, format!("no module `{bad}`"));
                file.push(" (x)\n");
            }
            2 => {
                let bad = *g.pick(&["Zz", "nope", "input"]);
                file.push(&format!("use {m} ({first}, "));
                file.mistake(bad, format!("`{m}` has no name `{bad}`"));
                file.push(")\n");
            }
            3 => {
                let bad = *g.pick(&["Option", "Result", "List", "eq", "alloc", "i64"]);
                file.push(&format!("use {m} ("));
                file.mistake(bad, format!("`{bad}` is in the prelude"));
                file.push(&format!(", {first})\n"));
            }
            4 => {
                file.push(&format!("use {m} ({first}, "));
                file.mistake(first, format!("`{first}` is listed twice"));
                file.push(")\n");
            }
            5 if !definable.is_empty() => {
                let own = *g.pick(&definable);
                file.push(&format!("use {m} ("));
                file.mistake(own, "this file defines it");
                file.push(")\n");
                file.defined.push(own.to_string());
            }
            _ => {
                file.push(&format!("use {m} ({first})\nuse "));
                file.mistake(&m, format!("`{m}` is already listed"));
                file.push(&format!(" ({first})\n"));
            }
        }
    }
    file.push("\nfn main() ! console {\n  print(\"hi\")\n}\n");
    for name in file.defined.clone() {
        file.push(&format!("\nfn {name}() {{\n}}\n"));
    }
    file
}

/// Each mistake in a `use` is one error at the place the generator wrote
/// it, the file with none is accepted, and no other error follows.
fn each_mistake_in_a_use_is_one_error_at_its_place() {
    generate::check(301, 200, uses, |file| match lower(file.source.as_bytes()) {
        Ok(_) => assert!(file.expected.is_empty(), "accepted"),
        Err(errors) => {
            let places: Vec<Pos> = errors.iter().map(|e| e.pos).collect();
            let wanted: Vec<Pos> = file.expected.iter().map(|(pos, _)| *pos).collect();
            assert_eq!(places, wanted, "{errors:?}");
            for (e, (_, what)) in errors.iter().zip(&file.expected) {
                assert!(e.message.contains(what.as_str()), "{e} lacks {what}");
            }
        }
    });
}

/// A `use` below another item is one error at the `use`, with a fix, and the
/// names it lists are not read.
fn a_use_after_another_item_is_one_error_at_the_use() {
    for module in library() {
        let name = &module.names[0].0;
        let before = format!("{HELLO}\n");
        let source = format!("{before}use std.{} ({name})\n", module.name);
        let Err(errors) = lower(source.as_bytes()) else {
            panic!("accepted: {source}");
        };
        assert_eq!(errors.len(), 1, "{errors:?}");
        assert_eq!(errors[0].pos, pos_after(&before), "{errors:?}");
        assert!(errors[0].message.contains("`use`"), "{errors:?}");
        assert!(errors[0].fix.is_some(), "{errors:?}");
    }
}

/// The compiler alone in a folder, with a file that imports from `std`.
fn alone(name: &str) -> PathBuf {
    let dir = crate::cli::folder(name);
    std::fs::copy(env!("CARGO_BIN_EXE_smith"), dir.join("smith")).unwrap();
    std::fs::write(
        dir.join("hello.smith"),
        "use std.console (console, print)\n\nfn main() ! console {\n  print(\"hello\")\n}\n",
    )
    .unwrap();
    dir
}

fn a_build_needs_no_file_beside_the_executable_to_import_from_std() {
    let dir = alone("alone-build");
    let built = crate::clock::output_within(
        Command::new(dir.join("smith"))
            .args(["build", "--target", any().name(), "hello.smith"])
            .current_dir(&dir),
        true,
    )
    .unwrap();
    assert!(built.status.success(), "{}", crate::cli::stderr(&built));
    assert!(dir.join(any().executable_name("hello")).is_file());
}

fn a_run_needs_no_file_beside_the_executable_to_import_from_std() {
    let dir = alone("alone-run");
    let ran = crate::clock::output_within(
        Command::new(dir.join("smith"))
            .args(["run", "hello.smith"])
            .current_dir(&dir),
        true,
    )
    .unwrap();
    assert!(ran.status.success(), "{}", crate::cli::stderr(&ran));
    assert_eq!(ran.stdout, b"hello\n");
}
