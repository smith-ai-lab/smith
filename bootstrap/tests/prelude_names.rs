//! A name is the program's to use unless the prelude lists it. The type
//! parameters of the prelude's source and of the modules of `std/` are names
//! inside that source and nowhere else, so a program defines a type, a trait,
//! a function, or a type parameter of its own under any of them. The names are
//! read from the source, so a parameter that is written later is covered.

use std::path::Path;
use std::sync::OnceLock;

use smith::diag;

use crate::Test;
use crate::generate::{self, Gen};
use crate::{enums, lambdas, numbers, programs, reprs, text, traits, tries, tuples};

pub const ONCE: &[Test] = &[
    (
        "a_program_names_an_item_as_a_type_parameter_of_the_prelude_or_of_std_is_named",
        a_program_names_an_item_as_a_type_parameter_of_the_prelude_or_of_std_is_named,
    ),
    (
        "a_mistake_in_a_program_that_uses_such_a_name_is_reported_at_the_mistake",
        a_mistake_in_a_program_that_uses_such_a_name_is_reported_at_the_mistake,
    ),
    (
        "no_error_is_at_line_zero_or_outside_the_file_the_error_is_in",
        no_error_is_at_line_zero_or_outside_the_file_the_error_is_in,
    ),
];

/// A name no source of the compiler uses for a type parameter.
const OTHER: &str = "Zq";

/// The second name a program writes, which is never the first.
const SECOND: &str = "Zp";

/// The sources a type parameter is written in: the prelude's, which the
/// compiler holds as text, and every module of `std/`.
fn sources() -> Vec<String> {
    let root = Path::new(env!("CARGO_MANIFEST_DIR"));
    let mut paths = vec![root.join("src/check.rs"), root.join("src/check/traits.rs")];
    let mut modules: Vec<_> = std::fs::read_dir(root.join("../std"))
        .unwrap()
        .map(|e| e.unwrap().path())
        .filter(|p| p.extension().is_some_and(|e| e == "smith"))
        .collect();
    modules.sort();
    paths.extend(modules);
    paths.iter().map(|p| std::fs::read_to_string(p).unwrap()).collect()
}

fn is_name(s: &str) -> bool {
    s.chars().next().is_some_and(|c| c.is_ascii_uppercase())
        && s.chars().all(|c| c.is_ascii_alphanumeric() || c == '_')
}

/// The names written as a type parameter after `enum`, `struct`, `trait`,
/// `fn`, or `impl` in the text, in `[...]`, less those some item of the text
/// is named: a name that is a type is not free.
fn parameters(text: &str, into: &mut Vec<String>, defined: &mut Vec<String>) {
    for line in text.lines() {
        let line = line.trim_start().trim_start_matches("pub ");
        let Some((keyword, rest)) = line.split_once(|c: char| !c.is_ascii_alphabetic()) else {
            continue;
        };
        if !["enum", "struct", "trait", "fn", "impl"].contains(&keyword) {
            continue;
        }
        let rest = if line[keyword.len()..].starts_with('[') {
            &line[keyword.len()..]
        } else {
            rest
        };
        let name: String = rest
            .chars()
            .take_while(|c| c.is_ascii_alphanumeric() || *c == '_')
            .collect();
        if keyword != "fn" && keyword != "impl" && is_name(&name) {
            defined.push(name.clone());
        }
        let after = &rest[name.len()..];
        let Some(open) = after.strip_prefix('[').or_else(|| rest.strip_prefix('[')) else {
            continue;
        };
        let Some(group) = open.split(']').next() else {
            continue;
        };
        for p in group.split(',') {
            let p = p.trim();
            let p = p.strip_prefix("out ").unwrap_or(p);
            let p = p.split(':').next().unwrap().trim();
            if is_name(p) && !into.iter().any(|n| n == p) {
                into.push(p.to_string());
            }
        }
    }
}

/// Every type parameter written in the prelude's source and in `std/`.
fn names() -> &'static [String] {
    static NAMES: OnceLock<Vec<String>> = OnceLock::new();
    NAMES.get_or_init(|| {
        let (mut found, mut defined) = (Vec::new(), Vec::new());
        for source in sources() {
            parameters(&source, &mut found, &mut defined);
        }
        // `Nat` is a type the prelude lists, though no source of it defines one.
        defined.push("Nat".to_string());
        found.retain(|n| !defined.contains(n));
        found.sort();
        found
    })
}

/// The kinds of item a program defines under a name.
const KINDS: [&str; 5] = ["a struct", "an enum", "a trait", "a function", "a type parameter"];

/// A program that defines an item of the kind under `name`, uses it, and
/// prints. `mistake` makes the last line a use of a name no one defines.
fn program(kind: usize, name: &str, mistake: bool) -> String {
    let (items, shown) = match kind {
        0 => (
            format!("struct {name} {{\n  v: i32\n}}\n"),
            format!("x: {name} = {name}(v: 3)\n  print(\"{{x.v}}\")"),
        ),
        1 => (
            format!("enum {name} {{\n  a\n  b: i32\n}}\n"),
            format!("e: {name} = .b(3)\n  print(\"{{e == e}}\")"),
        ),
        2 => (
            format!(
                "trait {name}[{SECOND}] {{\n  fn pass_on(x: {SECOND}) -> {SECOND}\n}}\n\nimpl {name}[i32] {{\n  fn pass_on(x: i32) -> i32 {{\n    x\n  }}\n}}\n"
            ),
            "n: i32 = 3\n  print(\"{pass_on(n)}\")".to_string(),
        ),
        3 => (
            format!("fn {name}(n: i32) -> i32 {{\n  n\n}}\n"),
            format!("print(\"{{{name}(3)}}\")"),
        ),
        _ => (
            format!("fn pass[{name}](x: take {name}) -> {name} {{\n  x\n}}\n"),
            "print(\"{pass(3)}\")".to_string(),
        ),
    };
    let last = if mistake { "\n  print(nope)" } else { "" };
    format!("{items}\nfn main() ! console, alloc {{\n  {shown}{last}\n}}\n")
}

fn printed(source: &str) -> Vec<u8> {
    let program = smith::lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
    smith::scalar::run(&program).stdout
}

/// What a program prints defining an item under a name of the prelude's or of
/// `std/`, and under another: the same.
fn a_program_names_an_item_as_a_type_parameter_of_the_prelude_or_of_std_is_named() {
    assert!(names().len() >= 7, "{:?}", names());
    for wanted in ["T", "U", "A", "B", "C", "R", "E"] {
        assert!(names().iter().any(|n| n == wanted), "{wanted} in {:?}", names());
    }
    for (kind, what) in KINDS.iter().enumerate() {
        let other = printed(&program(kind, OTHER, false));
        assert!(!other.is_empty(), "{}", program(kind, OTHER, false));
        // `F` is written nowhere now, and a program may still name its type so.
        for name in names().iter().map(String::as_str).chain(["F"]) {
            let source = program(kind, name, false);
            assert_eq!(printed(&source), other, "defining {} {name}\n{source}", what);
        }
    }
}

/// A mistake in the program is the one error, at the place it is written, with
/// the names of the prelude's type parameters defined by the program.
fn a_mistake_in_a_program_that_uses_such_a_name_is_reported_at_the_mistake() {
    generate::check(
        3,
        20,
        |g: &mut Gen| (g.below(KINDS.len()), g.pick(names()).clone()),
        |(kind, name)| {
            let source = program(*kind, name, true);
            let errors = smith::lower(source.as_bytes()).expect_err("a mistake");
            assert_eq!(errors.len(), 1, "{errors:?} for\n{source}");
            let at = source.find("nope").unwrap();
            let line = source[..at].matches('\n').count() + 1;
            let col = at - source[..at].rfind('\n').map_or(0, |n| n + 1) + 1;
            assert_eq!((errors[0].pos.line, errors[0].pos.col), (line, col), "{errors:?} for\n{source}");
            assert!(errors[0].message.contains("nope"), "{errors:?}");
        },
    );
}

/// An error is in the file, at a line it has, or in a module of the library,
/// at a place of its source. It is never in no file, and it is always shown.
fn no_error_is_at_line_zero_or_outside_the_file_the_error_is_in() {
    fn property(source: &[u8]) {
        let Err(errors) = smith::check(source) else { return };
        let lines = source.split(|b| *b == b'\n').count();
        for e in &errors {
            assert!(e.pos.line >= 1 && e.pos.col >= 1, "{e:?}");
            if e.module.is_none() {
                assert!(e.pos.line <= lines, "{e:?} beyond the {lines} lines");
            }
        }
        // Showing the errors is as much a part of reporting them.
        let shown = diag::render("f.smith", source, &errors);
        assert!(!shown.is_empty());
    }
    let own = |g: &mut Gen| {
        let (kind, name) = (g.below(KINDS.len()), g.pick(names()).clone());
        program(kind, &name, g.one_in(2)).into_bytes()
    };
    generate::check(5, 8, own, |s| property(s));
    generate::check(6, 4, |g| programs::program_with_mistake(g).source, |s| property(s));
    generate::check(7, 4, |g| traits::program_with_mistake(g).source.into_bytes(), |s| property(s));
    generate::check(8, 4, |g| enums::program_with_mistake(g).source.into_bytes(), |s| property(s));
    generate::check(9, 4, |g| tuples::program_with_mistake(g).source.into_bytes(), |s| property(s));
    generate::check(10, 4, |g| lambdas::program_with_mistake(g).source.into_bytes(), |s| property(s));
    generate::check(11, 4, |g| text::program_with_mistake(g).source.into_bytes(), |s| property(s));
    generate::check(12, 4, |g| numbers::program_with_mistake(g).source.into_bytes(), |s| property(s));
    generate::check(13, 4, |g| reprs::program_with_mistake(g).source.into_bytes(), |s| property(s));
    generate::check(14, 4, |g| tries::program_with_mistake(g).source.into_bytes(), |s| property(s));
}
