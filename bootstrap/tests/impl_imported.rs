//! An `impl` of a trait that a file imports lists the trait and not the
//! trait's functions. What two such traits that share a function's name
//! build, and what is said of an `impl` that lacks a function of the trait or
//! writes one it does not have.

use smith::diag::{Error, Pos};
use smith::timings::Timings;
use smith::{Project, compile_project_timed, lower_project};

use crate::Test;
use crate::generate::{self, Gen};
use crate::programs::pos_after;
use crate::targets::any;

pub const ONCE: &[Test] = &[
    (
        "impl_imported_a_trait_listed_without_its_function_is_implemented",
        impl_imported_a_trait_listed_without_its_function_is_implemented,
    ),
    (
        "impl_imported_two_traits_with_one_function_name_build_what_one_file_builds",
        impl_imported_two_traits_with_one_function_name_build_what_one_file_builds,
    ),
    (
        "impl_imported_each_mistake_is_one_error_at_its_place_that_names_the_function",
        impl_imported_each_mistake_is_one_error_at_its_place_that_names_the_function,
    ),
];

/// The names a module may have.
const NAMES: &[&str] = &["ui.animation", "db.render", "shapes", "store.cart.line"];

/// Two modules, each with a trait whose function is `show`, and a file that
/// implements both for its own types.
#[derive(Debug)]
struct Pair {
    modules: [&'static str; 2],
    /// What each implementation adds to the number it is given.
    numbers: [i32; 2],
    arg: i32,
    /// Whether the main lists the function `show` of the first trait too.
    lists: bool,
}

fn pair(g: &mut Gen) -> Pair {
    let first = g.below(NAMES.len());
    let second = (first + 1 + g.below(NAMES.len() - 1)) % NAMES.len();
    let number = |g: &mut Gen| g.below(90) as i32 + 1;
    Pair {
        modules: [NAMES[first], NAMES[second]],
        numbers: [number(g), number(g)],
        arg: number(g),
        lists: g.one_in(2),
    }
}

const TRAITS: [&str; 2] = ["Shown", "Drawn"];

fn module(i: usize) -> String {
    format!(
        "use std.bits (i32)\n\npub trait {}[T] {{\n  fn show(x: T) -> i32\n}}\n",
        TRAITS[i]
    )
}

fn main_text(p: &Pair) -> String {
    let [a, b] = p.modules;
    let list = if p.lists { ", show" } else { "" };
    let [ka, kb] = p.numbers;
    let x = p.arg;
    // The `use` lines are sorted by the path of the module.
    let mut uses = [
        (a, format!("Shown{list}")),
        (b, "Drawn".to_string()),
        ("std.bits", "i32".to_string()),
        ("std.console", "console, print".to_string()),
        ("std.int", "wrapping_add, wrapping_mul".to_string()),
    ];
    uses.sort();
    let uses: String = uses.iter().map(|(m, names)| format!("use {m} ({names})\n")).collect();
    format!(
        "{uses}\n\
         struct Mine {{\n  n: i32\n}}\n\n\
         struct Yours {{\n  n: i32\n}}\n\n\
         impl Shown[Mine] {{\n  fn show(x: Mine) -> i32 {{ wrapping_add(x.n, {ka}) }}\n}}\n\n\
         impl Drawn[Yours] {{\n  fn show(x: Yours) -> i32 {{ wrapping_mul(x.n, {kb}) }}\n}}\n\n\
         impl Drawn[Mine] {{\n  fn show(x: Mine) -> i32 {{ wrapping_mul(x.n, {kb}) }}\n}}\n\n\
         fn main() ! console, alloc {{\n  \
         print(\"{{Shown.show(Mine(n: {x}))}} {{Drawn.show(Yours(n: {x}))}} {{Drawn.show(Mine(n: {x}))}}\")\n}}\n"
    )
}

/// The same program in one file, whose two functions have names of their own.
fn one_file(p: &Pair) -> String {
    let [ka, kb] = p.numbers;
    let x = p.arg;
    format!(
        "use std.bits (i32)\nuse std.console (console, print)\nuse std.int (wrapping_add, wrapping_mul)\n\n\
         trait Shown[T] {{\n  fn show_a(x: T) -> i32\n}}\n\n\
         trait Drawn[T] {{\n  fn show_b(x: T) -> i32\n}}\n\n\
         struct Mine {{\n  n: i32\n}}\n\n\
         struct Yours {{\n  n: i32\n}}\n\n\
         impl Shown[Mine] {{\n  fn show_a(x: Mine) -> i32 {{ wrapping_add(x.n, {ka}) }}\n}}\n\n\
         impl Drawn[Yours] {{\n  fn show_b(x: Yours) -> i32 {{ wrapping_mul(x.n, {kb}) }}\n}}\n\n\
         impl Drawn[Mine] {{\n  fn show_b(x: Mine) -> i32 {{ wrapping_mul(x.n, {kb}) }}\n}}\n\n\
         fn main() ! console, alloc {{\n  \
         print(\"{{Shown.show_a(Mine(n: {x}))}} {{Drawn.show_b(Yours(n: {x}))}} {{Drawn.show_b(Mine(n: {x}))}}\")\n}}\n"
    )
}

/// The program of the issue, with the trait and not its function listed,
/// builds and prints what the function gives.
fn impl_imported_a_trait_listed_without_its_function_is_implemented() {
    let animation = "pub trait Shown[T] {\n  fn show(x: T) -> str\n}\n";
    let main = "use std.bits (i32)\nuse std.console (console, print)\nuse ui.animation (Shown)\n\n\
        struct Mine {\n  n: i32\n}\n\n\
        impl Shown[Mine] {\n  fn show(x: Mine) -> str { \"shown\" }\n}\n\n\
        fn main() ! console, alloc {\n  print(Shown.show(Mine(n: 1)))\n}\n";
    let program = lower_project(main.as_bytes(), &[("ui.animation", animation)])
        .unwrap_or_else(|e| panic!("{e:?}"));
    assert_eq!(smith::scalar::run(&program).stdout, b"shown\n");
}

/// A file that lists two traits of two modules with a function of one name
/// implements both for its own types, and builds what one file that defines
/// both traits builds.
fn impl_imported_two_traits_with_one_function_name_build_what_one_file_builds() {
    generate::check(30, 30, pair, |p| {
        let (a, b) = (module(0), module(1));
        // The modules hold one trait each, so the second names the first's.
        let files = |name: &str| {
            if name == p.modules[0] {
                Some(a.clone())
            } else if name == p.modules[1] {
                Some(b.clone())
            } else {
                None
            }
        };
        let project = Project {
            files: &files,
            entry: None,
        };
        let main = main_text(p);
        let one = one_file(p);
        let got = compile_project_timed(main.as_bytes(), &project, any(), &mut Timings::new())
            .unwrap_or_else(|e| panic!("{e:?}\n{main}"));
        let none = |_: &str| None;
        let alone = Project {
            files: &none,
            entry: None,
        };
        let wanted = compile_project_timed(one.as_bytes(), &alone, any(), &mut Timings::new())
            .unwrap_or_else(|e| panic!("{e:?}\n{one}"));
        assert!(got == wanted, "other bytes for\n{main}");
    });
}

/// An `impl` with a mistake, and where the error is.
#[derive(Debug)]
struct Mistake {
    /// Whether the function is lacking, and not an extra one written.
    lacks: bool,
    pad: usize,
}

fn placed(m: &Mistake) -> (String, Pos, &'static str) {
    let pad = "\n".repeat(m.pad);
    let head = format!(
        "{pad}use std.bits (i32)\nuse std.console (console, print)\nuse ui.animation (Shown)\n\nstruct Mine {{\n  n: i32\n}}\n\n"
    );
    let main = "fn main() ! console, alloc {\n  print(\"{Shown.show(Mine(n: 1))}\")\n}\n";
    if m.lacks {
        let before = head;
        let text = format!("{before}impl Shown[Mine] {{\n}}\n\n{main}");
        (text, pos_after(&format!("{before}impl ")), "`show`")
    } else {
        let before = format!(
            "{head}impl Shown[Mine] {{\n  fn show(x: Mine) -> i32 {{ x.n }}\n\n  fn "
        );
        let text = format!("{before}extra(x: Mine) -> i32 {{ x.n }}\n}}\n\n{main}");
        (text, pos_after(&before), "`extra`")
    }
}

/// An `impl` that lacks a function of an imported trait, or writes one the
/// trait does not have, is one error at its place that names the function as
/// the module wrote it, with no name the compiler made.
fn impl_imported_each_mistake_is_one_error_at_its_place_that_names_the_function() {
    generate::check(
        31,
        12,
        |g| Mistake {
            lacks: g.one_in(2),
            pad: g.below(4),
        },
        |m| {
            let (main, at, mentions) = placed(m);
            let animation = module(0);
            let files = [("ui.animation", animation.as_str())];
            let Err(errors) = lower_project(main.as_bytes(), &files) else {
                panic!("accepted:\n{main}");
            };
            assert_eq!(errors.len(), 1, "{errors:?} for\n{main}");
            let e: &Error = &errors[0];
            assert_eq!(e.pos, at, "{errors:?} for\n{main}");
            assert!(e.message.contains(mentions), "{e}");
            assert!(!e.message.contains('@'), "{e}");
        },
    );
}
