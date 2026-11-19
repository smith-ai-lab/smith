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
    (
        "a_function_of_a_module_written_in_smith_is_called_from_a_program",
        a_function_of_a_module_written_in_smith_is_called_from_a_program,
    ),
    (
        "a_program_prints_what_it_prints_with_the_items_of_the_module_pasted_into_it",
        a_program_prints_what_it_prints_with_the_items_of_the_module_pasted_into_it,
    ),
    (
        "the_names_a_module_does_not_export_are_its_own_and_clash_with_nothing",
        the_names_a_module_does_not_export_are_its_own_and_clash_with_nothing,
    ),
    (
        "a_name_that_is_not_pub_and_a_function_the_compiler_builds_are_refused_in_a_program",
        a_name_that_is_not_pub_and_a_function_the_compiler_builds_are_refused_in_a_program,
    ),
    (
        "a_function_of_a_module_calls_the_functions_the_compiler_builds",
        a_function_of_a_module_calls_the_functions_the_compiler_builds,
    ),
    (
        "only_what_a_program_reaches_is_read_checked_and_built",
        only_what_a_program_reaches_is_read_checked_and_built,
    ),
    (
        "an_error_in_a_function_a_program_reaches_is_at_its_place_in_the_module",
        an_error_in_a_function_a_program_reaches_is_at_its_place_in_the_module,
    ),
    (
        "a_function_of_a_module_is_inlined_and_folded_as_one_of_the_file_is",
        a_function_of_a_module_is_inlined_and_folded_as_one_of_the_file_is,
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
            // The prelude is read with every program and is not a module.
            .filter(|p| p.file_stem().is_some_and(|s| s != "prelude"))
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

/// A `main` that prints, with no `use` of its own.
const MAIN: &str = "fn main() ! console {\n  print(\"hi\")\n}\n";

/// A program that prints, with the `use` it needs.
const HELLO: &str = "use std.console (console, print)\n\nfn main() ! console {\n  print(\"hi\")\n}\n";

/// The names of the sizes, which `std.bits` holds and the files of `std/` do
/// not write with `pub`.
const SIZES: [&str; 6] = ["i16", "i32", "i8", "u16", "u32", "u64"];

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
        let source = numbers::with_header(format!(
            "use std.{} ({})\n\n{MAIN}",
            module.name,
            names.join(", ")
        ));
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

/// The names of the library that a source writes and does not define, each
/// with its module, in the order of the modules and then of the names.
fn names_used(source: &str) -> Vec<(&'static str, String)> {
    let (written, defined) = names_of(source);
    let mut used = Vec::new();
    for module in library() {
        let mut names: Vec<String> = (module.names.iter())
            .map(|(n, _)| n.clone())
            .chain(
                SIZES
                    .iter()
                    .filter(|_| module.name == "bits")
                    .map(|n| n.to_string()),
            )
            .filter(|n| written.contains(n) && !defined.contains(n))
            .collect();
        names.sort();
        used.extend(names.into_iter().map(|n| (module.name.as_str(), n)));
    }
    used
}

/// The source with a `use` line, at its top, for each module whose names it
/// writes, listing exactly those names, in the order the modules are in.
fn with_imports(source: &str) -> String {
    let mut lines = String::new();
    let used = names_used(source);
    let mut modules: Vec<&str> = used.iter().map(|(m, _)| *m).collect();
    modules.dedup();
    for module in modules {
        let names: Vec<&str> = (used.iter())
            .filter(|(m, _)| *m == module)
            .map(|(_, n)| n.as_str())
            .collect();
        lines.push_str(&format!("use std.{module} ({})\n", names.join(", ")));
    }
    if lines.is_empty() {
        return source.to_string();
    }
    format!("{lines}\n{source}")
}

/// The source without the `use` lines it starts with, and the blank lines
/// after them.
fn without_uses(source: &str) -> String {
    let mut rest = source;
    while rest.starts_with("use ") {
        rest = rest.split_once('\n').map_or("", |(_, after)| after);
    }
    rest.trim_start_matches('\n').to_string()
}

/// A program the generators build that has no mistake.
fn some_program(g: &mut Gen) -> String {
    match g.below(3) {
        0 => numbers::program(g).source,
        1 => text::program(g).source,
        _ => String::from_utf8(programs::program(g).source).unwrap(),
    }
}

/// A file that lists the names it uses, and only those, is built, and as the
/// same executable, with the same warnings, as the file with the `use` lines
/// that this test works out from what the file writes. The same file with no
/// `use` is refused, with one error for each name of the library that it
/// writes, which says the module the name is in and the `use` to add.
fn a_file_that_imports_the_names_it_uses_builds_what_it_built_without_the_use() {
    generate::check(300, 20, some_program, |source| {
        let bare = without_uses(source);
        let with = with_imports(&bare);
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
        let used = names_used(&bare);
        if used.is_empty() {
            assert_eq!(&bare, source);
            return;
        }
        let errors = check(bare.as_bytes()).expect_err("names that are not imported");
        assert_eq!(errors.len(), used.len(), "{errors:?} for\n{bare}");
        for (module, name) in &used {
            let wanted = format!("`{name}` is in `std.{module}`, and this file does not import it");
            let fix = format!("use std.{module} ({name})");
            assert!(
                errors.iter().any(|e| e.message.contains(&wanted)
                    && e.fix.as_deref().is_some_and(|f| f.contains(&fix))),
                "no error that says {wanted} in {errors:?} for\n{bare}"
            );
        }
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
            let source = numbers::with_header(format!("{head}{name})\n\n{bare}"));
            let warnings = check(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
            let places: Vec<Pos> = warnings.iter().map(|w| w.pos).collect();
            // The `use` lines are in the order of the modules, so the line is
            // the first, or the second where the `use` of `std.console` is above.
            let line = source.lines().position(|l| l.starts_with(&head)).unwrap() + 1;
            let at = Pos {
                line,
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
                let bad = *g.pick(&["Option", "Result", "List", "eq", "alloc", "Int"]);
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
    file.push("\nfn main() {\n}\n");
    for name in file.defined.clone() {
        file.push(&format!("\nfn {name}() {{\n}}\n"));
    }
    file
}

/// Each mistake in a `use` is one error at the place the generator wrote
/// it, the file with none is accepted, and no other error follows.
fn each_mistake_in_a_use_is_one_error_at_its_place() {
    generate::check(301, 70, uses, |file| match lower(file.source.as_bytes()) {
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
        let before = "fn main() {\n}\n\n".to_string();
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
    crate::executable::copy(env!("CARGO_BIN_EXE_smith"), &dir.join("smith"));
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

/// What a program of the scalar form prints, after the passes of a build.
fn printed(program: &smith::scalar::Program) -> Vec<u8> {
    smith::scalar::run(program).stdout
}

/// The program lowered with the library of the modules, each a name and a
/// source.
fn built_with(source: &str, library: &[(&str, &str)]) -> smith::scalar::Program {
    smith::lower_in(source.as_bytes(), &with_std(library))
        .unwrap_or_else(|e| panic!("{e:?} for\n{source}\n{library:?}"))
}

/// The modules given, and the modules of the standard library that the
/// programs here import beside them, which `lower_in` does not read unless
/// they are given.
fn with_std<'a>(library: &[(&'a str, &'a str)]) -> Vec<(&'a str, &'a str)> {
    let mut all = library.to_vec();
    for module in ["bits", "console", "int"] {
        if !all.iter().any(|(name, _)| *name == module) {
            all.push((module, smith::library_text(module)));
        }
    }
    all
}

/// A module whose items call each other, and the program that imports some of
/// its names. `own` is the name of the function of the module the others call,
/// and `kind` the name of its struct, which the module need not export.
#[derive(Debug)]
struct Written {
    /// The module, with each item written with `pub` where it is exported.
    module: String,
    /// The same items with no `pub`, written as a file holds them.
    pasted: String,
    /// What the program prints, in the order it prints.
    body: String,
    /// The names the program imports.
    imports: Vec<&'static str>,
}

fn written(g: &mut Gen, own: &str, kind: &str) -> Written {
    let k = |g: &mut Gen| g.below(50) as i32 + 1;
    let (k1, k2, k3, a, b) = (k(g), k(g), k(g), k(g), k(g));
    let shown = g.one_in(2);
    // Which of the items the module exports, besides the three the program calls.
    let kind_pub = shown && kind == "Pt";
    let items = |p: &str, kind_p: &str| {
        format!(
            "{p}fn {own}(n: i32) -> i32 {{ wrapping_add(n, {k1}) }}\n\n\
             {p}fn twice(n: i32) -> i32 {{ {own}({own}(n)) }}\n\n\
             {kind_p}struct {kind} {{\n  {p}x: i32\n  {p}y: i32\n}}\n\n\
             {p}enum Shape {{\n  dot\n  line: i32\n}}\n\n\
             {p}trait Area[T] {{\n  fn area(s: T) -> i32\n}}\n\n\
             impl Area[{kind}] {{\n  fn area(s: {kind}) -> i32 {{ wrapping_add(twice(s.x), s.y) }}\n}}\n\n\
             impl Area[Shape] {{\n  fn area(s: Shape) -> i32 {{\n    match s {{\n      .dot {{ {k2} }}\n      .line(n) {{ {own}(n) }}\n    }}\n  }}\n}}\n\n\
             {p}fn make(a: i32) -> {kind} {{ {kind}(x: a, y: {k3}) }}\n\n\
             {p}fn measure(s: Shape) -> i32 {{ wrapping_add(area(s), area(make({k1}))) }}\n"
        )
    };
    Written {
        module: numbers::with_header(items("pub ", if kind_pub { "pub " } else { "" })),
        pasted: items("", ""),
        body: format!(
            "  print(\"{{twice({a})}} {{measure(.dot)}} {{measure(.line({b}))}} {{make({a}).y}}\")\n"
        ),
        imports: vec!["Area", "make", "measure", "twice"],
    }
}

/// A program that imports from a module of the library, and calls what it
/// imports, prints what the same program prints with the module's items in the
/// file in place of its `use`.
fn a_program_prints_what_it_prints_with_the_items_of_the_module_pasted_into_it() {
    generate::check(40, 20, |g| written(g, "helper", "Pt"), |w| {
        let program = format!("fn main() ! console, alloc {{\n{}}}\n", w.body);
        let uses = numbers::with_header(format!("use std.gen ({})\n\n{program}", w.imports.join(", ")));
        let pasted = numbers::with_header(format!("{}\n{program}", w.pasted));
        let got = built_with(&uses, &[("gen", &w.module)]);
        let wanted = built_with(&pasted, &[]);
        assert_eq!(printed(&got), printed(&wanted), "for\n{uses}\n{}", w.module);
        assert!(!printed(&got).is_empty());
    });
}

/// A name of a module that the program does not list is the module's own: a
/// program defines the same names for itself, and the module's functions still
/// call the module's.
fn the_names_a_module_does_not_export_are_its_own_and_clash_with_nothing() {
    generate::check(40, 20, |g| written(g, "helper", "Pt"), |w| {
        let own = "fn helper(n: i32) -> i32 { wrapping_mul(n, 1000) }\n\nstruct Pt {\n  z: i32\n}\n\n";
        let mine = "  print(\"{helper(2)} {Pt(z: 3).z}\")\n";
        let program = |body: &str| format!("fn main() ! console, alloc {{\n{mine}{body}}}\n");
        let with = numbers::with_header(format!(
            "use std.gen ({})\n\n{own}{}",
            w.imports.join(", "),
            program(&w.body)
        ));
        let got = printed(&built_with(&with, &[("gen", &w.module)]));
        let wanted = printed(&built_with(
            &numbers::with_header(format!(
                "{}\n{own}{}",
                w.pasted.replace("helper", "mhelper").replace("Pt", "MPt"),
                program(&w.body)
            )),
            &[],
        ));
        assert_eq!(got, wanted, "for\n{with}");
        assert!(got.starts_with(b"2000 3"), "{}", String::from_utf8_lossy(&got));
    });
}

/// The example of the issue: `is_digit`, written in Smith, called from a program.
fn a_function_of_a_module_written_in_smith_is_called_from_a_program() {
    let ascii = "pub fn is_digit(b: Byte) -> bool { b >= '0' and b <= '9' }\n";
    let source = numbers::with_header("use std.ascii (is_digit)\n\nfn main() ! console, alloc {\n  print(\"{is_digit('7')} {is_digit('x')}\")\n}\n".to_string());
    assert_eq!(printed(&built_with(&source, &[("ascii", ascii)])), b"true false\n");
}

/// A name of a module that is not `pub` cannot be listed, and its name is not
/// the name of anything in the program; a function the compiler builds is
/// refused in a program as it is without a library.
fn a_name_that_is_not_pub_and_a_function_the_compiler_builds_are_refused_in_a_program() {
    generate::check(20, 10, |g| written(g, "helper", "Pt"), |w| {
        let module = format!("{}\nfn secret(n: i32) -> i32 {{ n }}\n", w.module);
        let library = [("gen", module.as_str())];
        let main = |body: &str| format!("fn main() ! console, alloc {{\n{body}  print(\"x\")\n}}\n");
        let with = numbers::with_header;
        let refused = |source: String, mention: &str| {
            let errors = smith::lower_in(source.as_bytes(), &with_std(&library))
                .expect_err(&format!("refused: {source}"));
            assert!(errors.iter().any(|e| e.message.contains(mention)), "{errors:?} for {source}");
        };
        refused(with(format!("use std.gen (secret)\n\n{}", main(""))), "secret");
        refused(with(main("  n = secret(1)\n")), "secret");
        refused(with(main("  n = helper(1)\n")), "helper");
        refused(with(main("  n = __wrapping_add(1, 2)\n")), "__wrapping_add");
    });
}

/// A function of a module calls the private functions of its module and the
/// functions the compiler builds.
fn a_function_of_a_module_calls_the_functions_the_compiler_builds() {
    let module = "fn base() -> Byte { 200 }\n\npub fn plus(a: Byte) -> Byte { __wrapping_add(a, base()) }\n";
    let source = numbers::with_header("use std.gen (plus)\n\nfn main() ! console, alloc {\n  print(\"{plus(100)}\")\n}\n".to_string());
    assert_eq!(printed(&built_with(&source, &[("gen", module)])), b"44\n");
}

/// A module that is not imported is not read, a function that is not reached
/// is not checked, and an import that is not called adds nothing.
fn only_what_a_program_reaches_is_read_checked_and_built() {
    generate::check(20, 10, |g| written(g, "helper", "Pt"), |w| {
        let broken_file = "pub fn x( {{{\n";
        let unreached = format!("{}\npub fn bad(n: i32) -> i32 {{ nothing_here(n) }}\n", w.module);
        let program = format!("fn main() ! console, alloc {{\n{}}}\n", w.body);
        let with = numbers::with_header(format!("use std.gen ({})\n\n{program}", w.imports.join(", ")));
        let library = [("gen", unreached.as_str()), ("broken", broken_file)];
        assert_eq!(built_with(&with, &library), built_with(&with, &[("gen", &w.module)]));
        let hello = "fn main() ! console, alloc {\n  print(\"hi\")\n}\n";
        let imported = numbers::with_header(format!("use std.gen (measure, bad)\n\n{hello}"));
        assert_eq!(built_with(&imported, &library), built_with(&numbers::with_header(hello.to_string()), &[]));
    });
}

/// An error in a function the program reaches is at its place in the file of
/// the module, and says which module.
fn an_error_in_a_function_a_program_reaches_is_at_its_place_in_the_module() {
    generate::check(30, 10, |g| (g.below(5), g.below(6), g.below(5)), |&(lines, indent, name)| {
        let name = ["nonesuch", "missing_one", "gone", "n_a", "no_such"][name];
        let head = format!(
            "use std.bits (i32)\n\n{}pub fn bad(n: i32) -> i32 {{\n{}",
            "\n".repeat(lines),
            " ".repeat(indent)
        );
        let module = format!("{head}{name}(n)\n}}\n");
        let source = numbers::with_header("use std.gen (bad)\n\nfn main() ! console, alloc {\n  print(\"{bad(1)}\")\n}\n".to_string());
        let errors = smith::lower_in(source.as_bytes(), &with_std(&[("gen", &module)])).unwrap_err();
        let found: Vec<_> = errors.iter().filter(|e| e.message.contains(name)).collect();
        assert_eq!(found.len(), 1, "{errors:?}");
        assert_eq!(found[0].module.as_deref(), Some("gen"));
        assert_eq!(found[0].pos, pos_after(&head), "{errors:?}");
    });
}

/// A call of a function of a module is a compare or two in the scalar form,
/// as the same function written in the program's file is.
fn a_function_of_a_module_is_inlined_and_folded_as_one_of_the_file_is() {
    let ascii = "pub fn is_digit(b: Byte) -> bool { b >= '0' and b <= '9' }\n";
    let main = "fn main() ! console, alloc {\n  print(\"{is_digit('7')} {is_digit('x')}\")\n}\n";
    let imported = built_with(
        &numbers::with_header(format!("use std.ascii (is_digit)\n\n{main}")),
        &[("ascii", ascii)],
    );
    let own = built_with(&numbers::with_header(format!("{}\n{main}", ascii.replace("pub ", ""))), &[]);
    let build = |p: &smith::scalar::Program| {
        smith::scalar::remove(&smith::scalar::common(&smith::scalar::fold(&smith::scalar::inline(p))))
    };
    assert_eq!(build(&imported), build(&own));
}
