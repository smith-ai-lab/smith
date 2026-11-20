//! Finds the compiler's tests, so that adding a file of tests adds a line to
//! no file that another file of tests also adds to.
//!
//! Every `.rs` file in `tests/`, except `main.rs`, is a file of tests, and
//! says which group each of its tests is in by declaring `pub const ONCE`,
//! `MACHINE`, or `RUNS`, as `main.rs` says. A file that declares none is an
//! error, so that no test is left out of every run. Code that tests share, and
//! holds no test, is in `tests/support/`, and every file there is a module too.
//! A file whose first lines are `#![cfg(..)]` is a module only where they hold.
//!
//! The result is `tests.rs` in the build folder, which `main.rs` includes.

use std::fmt::Write;
use std::path::{Path, PathBuf};

#[path = "src/divide.rs"]
mod divide;

/// The groups, as the constant a file declares and the function that gives
/// every test of the group.
const GROUPS: &[(&str, &str)] = &[("ONCE", "once"), ("MACHINE", "machine"), ("RUNS", "hang")];

fn main() {
    let root = PathBuf::from(std::env::var_os("CARGO_MANIFEST_DIR").unwrap());
    standard_library(&root);
    prelude(&root);
    let tests = root.join("tests");
    println!("cargo:rerun-if-changed=tests");
    println!("cargo:rerun-if-changed=build.rs");

    let files = rust_files(&tests);
    let support = rust_files(&tests.join("support"));
    let mut modules = String::new();
    let mut groups: Vec<(&str, String)> = GROUPS.iter().map(|(_, f)| (*f, String::new())).collect();
    for (file, is_test) in files
        .iter()
        .filter(|f| f.file_name().unwrap() != "main.rs")
        .map(|f| (f, true))
        .chain(support.iter().map(|f| (f, false)))
    {
        let name = file.file_stem().unwrap().to_str().unwrap();
        let text = std::fs::read_to_string(file).unwrap();
        let cfg: String = text
            .lines()
            .take_while(|l| l.trim().is_empty() || l.starts_with("//!") || l.starts_with("#!["))
            .filter(|l| l.starts_with("#![cfg("))
            .filter_map(|l| l.strip_prefix("#!"))
            .map(|l| format!("#{l}\n"))
            .collect();
        writeln!(
            modules,
            "{cfg}#[path = {:?}]\nmod {name};",
            file.display().to_string()
        )
        .unwrap();
        let declared: Vec<usize> = GROUPS
            .iter()
            .enumerate()
            .filter(|(_, (constant, _))| {
                text.lines()
                    .any(|l| l.starts_with(&format!("pub const {constant}: &[Test]")))
            })
            .map(|(i, _)| i)
            .collect();
        if is_test && declared.is_empty() {
            panic!(
                "{} is in no group: it declares none of {}, and a file with no test in it \
                 belongs in tests/support/",
                file.display(),
                GROUPS
                    .iter()
                    .map(|(c, _)| *c)
                    .collect::<Vec<_>>()
                    .join(", ")
            );
        }
        if !is_test && !declared.is_empty() {
            panic!("{} is in tests/support/ and declares tests", file.display());
        }
        for i in declared {
            writeln!(
                groups[i].1,
                "{cfg}    all.extend_from_slice({name}::{});",
                GROUPS[i].0
            )
            .unwrap();
        }
    }
    let mut out = modules;
    for (function, body) in &groups {
        write!(
            out,
            "\n/// Every test of the group `{function}`.\n\
             pub fn {function}_tests() -> Vec<Test> {{\n    let mut all: Vec<Test> = Vec::new();\n\
             {body}    all\n}}\n"
        )
        .unwrap();
    }
    let dest = PathBuf::from(std::env::var_os("OUT_DIR").unwrap()).join("tests.rs");
    std::fs::write(dest, out).unwrap();
}

/// The `.rs` files directly in `dir`, in order of name.
fn rust_files(dir: &Path) -> Vec<PathBuf> {
    let mut found: Vec<PathBuf> = std::fs::read_dir(dir)
        .unwrap()
        .map(|e| e.unwrap().path())
        .filter(|p| p.extension().is_some_and(|e| e == "rs"))
        .collect();
    found.sort();
    found
}

/// Holds the source of the standard library, which is the `.smith` files in
/// `std/` at the root of the repository, inside the executable. The result is
/// `std_modules.rs` in the build folder, a function that gives a module's
/// source by its name, so that looking one up does not depend on how many
/// there are, and one that is not looked up is never read.
fn standard_library(root: &Path) {
    let std = root.join("..").join("std");
    println!("cargo:rerun-if-changed=../std");
    let mut files: Vec<PathBuf> = std::fs::read_dir(&std)
        .unwrap()
        .map(|e| e.unwrap().path())
        .filter(|p| p.extension().is_some_and(|e| e == "smith"))
        // The prelude is read with every program, not imported.
        .filter(|p| p.file_stem().is_some_and(|s| s != "prelude"))
        .collect();
    files.sort();
    let mut out = String::from(
        "/// The source of the module of the standard library with the name.\n\
         fn library_source(module: &str) -> Option<&'static str> {\n    match module {\n",
    );
    for file in &files {
        let name = file.file_stem().unwrap().to_str().unwrap();
        writeln!(
            out,
            "        {name:?} => Some(include_str!({:?})),",
            file.canonicalize().unwrap().display().to_string()
        )
        .unwrap();
    }
    out.push_str("        _ => None,\n    }\n}\n");
    let names: Vec<String> = files
        .iter()
        .map(|f| format!("{:?}", f.file_stem().unwrap().to_str().unwrap()))
        .collect();
    writeln!(
        out,
        "\n/// The names of the modules of the standard library, in order.\n\
         const LIBRARY_MODULES: &[&str] = &[{}];",
        names.join(", ")
    )
    .unwrap();
    // The names each module exports, so that a name used without an import
    // is told which module to import it from without reading any module.
    out.push_str(
        "\n/// The names each module of the standard library exports: those of its items\n\
         /// written with `pub`, and the functions of its traits.\n\
         const STD_EXPORTS: &[(&str, &[&str])] = &[\n",
    );
    for file in &files {
        let name = file.file_stem().unwrap().to_str().unwrap();
        let exported = exports(&std::fs::read_to_string(file).unwrap());
        writeln!(out, "    ({name:?}, &{exported:?}),").unwrap();
    }
    out.push_str("];\n");
    let dest = PathBuf::from(std::env::var_os("OUT_DIR").unwrap()).join("std_modules.rs");
    std::fs::write(dest, out).unwrap();
}

/// The names of the items a module writes with `pub`, and of the functions of
/// its public traits and the operations of its public effects, read from the lines that start them.
fn exports(source: &str) -> Vec<String> {
    let name = |rest: &str| -> String {
        (rest.chars())
            .take_while(|c| c.is_ascii_alphanumeric() || *c == '_')
            .collect()
    };
    let mut found = Vec::new();
    // What the indented `fn` lines of the item that is open are: those of a
    // trait are all exported, and those of an effect are when they are `pub`.
    let mut open: Option<&str> = None;
    for line in source.lines() {
        if let Some(rest) = ["pub trait ", "pub effect "]
            .iter()
            .find_map(|k| line.strip_prefix(k))
        {
            found.push(name(rest));
            open = Some(if line.starts_with("pub trait ") {
                "fn "
            } else {
                "pub fn "
            });
        } else if let Some(rest) = ["pub fn ", "pub struct ", "pub enum "]
            .iter()
            .find_map(|k| line.strip_prefix(k))
        {
            found.push(name(rest));
        } else if open.is_some() && line == "}" {
            open = None;
        } else if let Some(start) = open
            && let Some(rest) = line.trim_start().strip_prefix(start)
        {
            found.push(name(rest));
        }
    }
    found.sort();
    found.dedup();
    found
}

/// Divides the prelude, which is `std/prelude.smith`, into its items, so that
/// an `impl` is found by the trait and the types it is for and is read only
/// when a program reaches it. The result is `prelude.rs` in the build folder:
/// the text of the items that are not `impl`s, which every program reads, and
/// each `impl` with its names.
fn prelude(root: &Path) {
    let file = root.join("..").join("std").join("prelude.smith");
    println!("cargo:rerun-if-changed=../std/prelude.smith");
    let source = std::fs::read_to_string(&file).unwrap();
    let divided = divide::divide(&source);
    let mut out = format!(
        "/// The prelude's items that are not `impl`s.\n\
         const PRELUDE_DECLARATIONS: &str = {:?};\n\n\
         /// The prelude's `impl`s, in the order written.\n\
         const PRELUDE_IMPLS: &[PreludeImpl<'static>] = &[\n",
        divided.declarations
    );
    for im in &divided.impls {
        writeln!(
            out,
            "    PreludeImpl {{ source: {:?}, name: {:?}, types: &{:?} }},",
            im.source, im.name, im.types
        )
        .unwrap();
    }
    out.push_str("];\n");
    let dest = PathBuf::from(std::env::var_os("OUT_DIR").unwrap()).join("prelude.rs");
    std::fs::write(dest, out).unwrap();
}
