//! `as` after a name in a `use`, a module imported by its name or under
//! another with `as`, and a function of a trait written with its trait. What
//! each form prints, what each form adds to the executable, and what is said
//! of each mistake in one.

use smith::diag::{Error, Pos};
use smith::timings::Timings;
use smith::{Project, compile_project_timed, lower_project};

use crate::Test;
use crate::generate::{self, Gen};
use crate::programs::pos_after;
use crate::targets::any;

pub const ONCE: &[Test] = &[
    (
        "aliases_two_modules_that_export_one_name_give_what_two_names_give_in_one_file",
        aliases_two_modules_that_export_one_name_give_what_two_names_give_in_one_file,
    ),
    (
        "aliases_every_way_to_write_a_name_of_a_module_builds_the_same_executable",
        aliases_every_way_to_write_a_name_of_a_module_builds_the_same_executable,
    ),
    (
        "aliases_a_function_two_traits_share_is_written_with_its_trait",
        aliases_a_function_two_traits_share_is_written_with_its_trait,
    ),
    (
        "aliases_each_mistake_is_one_error_at_its_place",
        aliases_each_mistake_is_one_error_at_its_place,
    ),
    (
        "aliases_the_examples_of_the_pages_give_what_the_pages_say",
        aliases_the_examples_of_the_pages_give_what_the_pages_say,
    ),
];

/// What a program of the scalar form prints.
fn printed(program: &smith::scalar::Program) -> Vec<u8> {
    smith::scalar::run(program).stdout
}

/// The names a module may have. Two of them end alike.
const NAMES: &[&str] = &[
    "geo",
    "shapes.circle",
    "util.math",
    "store.cart.line",
    "ui.util",
    "db.util",
];

/// The name a module is written as when it is imported by its name.
fn last(module: &str) -> &str {
    module.rsplit('.').next().unwrap_or(module)
}

/// Two modules that export the same names, and what is passed to them.
#[derive(Debug)]
struct Pair {
    modules: [&'static str; 2],
    /// The numbers each module's functions hold, three for each.
    numbers: [[i32; 3]; 2],
    arg: i32,
    /// How the file that is built imports them.
    form: usize,
}

const FORMS: usize = 4;

fn pair(g: &mut Gen) -> Pair {
    let first = g.below(NAMES.len());
    let second = (first + 1 + g.below(NAMES.len() - 1)) % NAMES.len();
    let number = |g: &mut Gen| g.below(90) as i32 + 1;
    Pair {
        modules: [NAMES[first], NAMES[second]],
        numbers: [
            [number(g), number(g), number(g)],
            [number(g), number(g), number(g)],
        ],
        arg: number(g),
        form: g.below(FORMS),
    }
}

/// What a module that holds the functions of `items` imports.
const ITEMS_USES: &str = "use std.bits (i32)\nuse std.int (wrapping_add, wrapping_mul)\n\n";

/// The module `i` of a pair as a file: it exports `f` and `g`, and has a
/// private function that both modules name alike. Written for one file,
/// every name has a number after it.
fn items(p: &Pair, i: usize, separate: bool) -> String {
    let [k0, k1, k2] = p.numbers[i];
    let (public, helper, f, g) = if separate {
        ("pub ", "helper".to_string(), "f".to_string(), "g".to_string())
    } else {
        ("", format!("helper{i}"), format!("f{i}"), format!("g{i}"))
    };
    format!(
        "fn {helper}(n: i32) -> i32 {{ wrapping_mul(n, {k0}) }}\n\n\
         {public}fn {g}(n: i32) -> i32 {{ wrapping_add({helper}(n), {k1}) }}\n\n\
         {public}fn {f}(n: i32) -> i32 {{ {g}(wrapping_add(n, {k2})) }}\n\n"
    )
}

/// The file that is built, which imports the pair as `form` says and prints
/// what the four functions give. `names` are the four names each function is
/// written as: `f` and `g` of the first module, then those of the second.
fn main_text(p: &Pair, form: usize) -> String {
    let [a, b] = p.modules;
    let (head, names): (String, [String; 4]) = match form {
        0 => (
            format!("use {a} (f as fa, g as ga)\nuse {b} (f as fb, g as gb)\n"),
            ["fa", "ga", "fb", "gb"].map(String::from),
        ),
        1 => (
            format!("use {a} (f, g)\nuse {b} (f as fb, g as gb)\n"),
            ["f", "g", "fb", "gb"].map(String::from),
        ),
        2 => {
            let first = last(a);
            (
                format!("use {a}\nuse {b} as other\n"),
                [
                    format!("{first}.f"),
                    format!("{first}.g"),
                    "other.f".to_string(),
                    "other.g".to_string(),
                ],
            )
        }
        _ => (
            format!("use {a} as one\nuse {b} as two\n"),
            ["one.f", "one.g", "two.f", "two.g"].map(String::from),
        ),
    };
    let x = p.arg;
    let [fa, ga, fb, gb] = names;
    // The `use` lines, sorted by the path of the module.
    let mut lines: Vec<(&str, String)> = head
        .lines()
        .map(|l| (l.split(' ').nth(1).unwrap_or(""), l.to_string()))
        .collect();
    lines.push(("std.bits", "use std.bits (i32)".to_string()));
    lines.push(("std.console", "use std.console (console, print)".to_string()));
    lines.push(("std.int", "use std.int (wrapping_add)".to_string()));
    lines.sort();
    let head: String = lines.iter().map(|(_, l)| format!("{l}\n")).collect();
    format!(
        "{head}\nfn helper(n: i32) -> i32 {{ wrapping_add(n, 7) }}\n\n\
         fn main() ! console, alloc {{\n  \
         print(\"{{{fa}({x})}} {{{ga}({x})}} {{{fb}({x})}} {{{gb}({x})}} {{helper({x})}}\")\n}}\n"
    )
}

/// The same file with every name of a module written as one file has them.
fn one_file(p: &Pair) -> String {
    let x = p.arg;
    format!(
        "use std.bits (i32)\nuse std.console (console, print)\nuse std.int (wrapping_add, wrapping_mul)\n\n{}{}fn helper(n: i32) -> i32 {{ wrapping_add(n, 7) }}\n\n\
         fn main() ! console, alloc {{\n  \
         print(\"{{f0({x})}} {{g0({x})}} {{f1({x})}} {{g1({x})}} {{helper({x})}}\")\n}}\n",
        items(p, 0, false),
        items(p, 1, false)
    )
}

/// Every pair of modules that export one name: importing one under another
/// name, importing the modules and writing the name with the module, and
/// importing each under a name of its own, give what the two functions give
/// under different names in one file.
fn aliases_two_modules_that_export_one_name_give_what_two_names_give_in_one_file() {
    generate::check(20, 40, pair, |p| {
        let modules: Vec<String> = (0..2).map(|i| format!("{ITEMS_USES}{}", items(p, i, true))).collect();
        let files: Vec<(&str, &str)> = p
            .modules
            .iter()
            .zip(&modules)
            .map(|(name, text)| (*name, text.as_str()))
            .collect();
        let main = main_text(p, p.form);
        let got = lower_project(main.as_bytes(), &files)
            .unwrap_or_else(|e| panic!("{e:?}\n{main}\n{files:?}"));
        let one = one_file(p);
        let wanted = smith::lower(one.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{one}"));
        assert_eq!(printed(&got), printed(&wanted), "for\n{main}\n{files:?}");
        assert!(!printed(&got).is_empty());
    });
}

/// An alias and a name written with its module are only the name of what
/// they say, so the executable is the same however a file imports the same
/// functions.
fn aliases_every_way_to_write_a_name_of_a_module_builds_the_same_executable() {
    generate::check(21, 12, pair, |p| {
        let modules: Vec<String> = (0..2).map(|i| format!("{ITEMS_USES}{}", items(p, i, true))).collect();
        let files = |name: &str| {
            p.modules
                .iter()
                .zip(&modules)
                .find(|(m, _)| **m == name)
                .map(|(_, text)| text.clone())
        };
        let project = Project {
            files: &files,
            entry: None,
        };
        let built: Vec<Vec<u8>> = (0..FORMS)
            .map(|form| {
                let main = main_text(p, form);
                compile_project_timed(main.as_bytes(), &project, any(), &mut Timings::new())
                    .unwrap_or_else(|e| panic!("{e:?}\n{main}"))
            })
            .collect();
        for form in 1..FORMS {
            assert!(
                built[0] == built[form],
                "form {form} builds other bytes than form 0 for {p:?}"
            );
        }
    });
}

/// The module of a struct and a trait over it, which the main reads, and the
/// module of a second trait over the same struct whose function has the same
/// name.
fn shapes(k: i32) -> String {
    format!(
        "use std.bits (i32)\nuse std.int (wrapping_add)\n\n\
         pub struct S {{\n  pub w: i32\n}}\n\n\
         pub fn make(w: i32) -> S {{ S(w: w) }}\n\n\
         pub trait TA[X] {{\n  fn size(x: X) -> i32\n}}\n\n\
         impl TA[S] {{\n  fn size(x: S) -> i32 {{ wrapping_add(x.w, {k}) }}\n}}\n"
    )
}

fn solids(k: i32) -> String {
    format!(
        "use shapes (S)\nuse std.bits (i32)\nuse std.int (wrapping_mul)\n\n\
         pub trait TB[X] {{\n  fn size(x: X) -> i32\n}}\n\n\
         impl TB[S] {{\n  fn size(x: S) -> i32 {{ wrapping_mul(x.w, {k}) }}\n}}\n"
    )
}

/// Two modules whose traits both have `size`, what the numbers are, and what
/// is passed.
#[derive(Debug)]
struct Traits {
    numbers: [i32; 2],
    arg: i32,
    /// Whether the main lists the function `size` of the first trait too.
    lists: bool,
}

fn traits(g: &mut Gen) -> Traits {
    let number = |g: &mut Gen| g.below(90) as i32 + 1;
    Traits {
        numbers: [number(g), number(g)],
        arg: number(g),
        lists: g.one_in(2),
    }
}

/// Two traits that have a function of one name are told apart by writing the
/// trait, whether the file lists the function of one of them or not, and
/// each gives what a function of its own name gives in one file.
fn aliases_a_function_two_traits_share_is_written_with_its_trait() {
    generate::check(22, 40, traits, |t| {
        let [ka, kb] = t.numbers;
        let (a, b) = (shapes(ka), solids(kb));
        let files = [("shapes", a.as_str()), ("solids", b.as_str())];
        let list = if t.lists { ", size" } else { "" };
        let x = t.arg;
        let main = format!(
            "use shapes (S, TA, make{list})\nuse solids (TB)\nuse std.console (console, print)\n\n\
             fn main() ! console, alloc {{\n  \
             print(\"{{TA.size(make({x}))}} {{TB.size(make({x}))}}\")\n}}\n"
        );
        let got = lower_project(main.as_bytes(), &files)
            .unwrap_or_else(|e| panic!("{e:?}\n{main}\n{files:?}"));
        let one = format!(
            "use std.bits (i32)\nuse std.console (console, print)\nuse std.int (wrapping_add, wrapping_mul)\n\n\
             struct S {{\n  w: i32\n}}\n\n\
             trait TA[X] {{\n  fn size_a(x: X) -> i32\n}}\n\n\
             impl TA[S] {{\n  fn size_a(x: S) -> i32 {{ wrapping_add(x.w, {ka}) }}\n}}\n\n\
             trait TB[X] {{\n  fn size_b(x: X) -> i32\n}}\n\n\
             impl TB[S] {{\n  fn size_b(x: S) -> i32 {{ wrapping_mul(x.w, {kb}) }}\n}}\n\n\
             fn main() ! console, alloc {{\n  \
             print(\"{{size_a(S(w: {x}))}} {{size_b(S(w: {x}))}}\")\n}}\n"
        );
        let wanted = smith::lower(one.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{one}"));
        assert_eq!(printed(&got), printed(&wanted), "for\n{main}\n{files:?}");
        assert!(!printed(&got).is_empty());
    });
}

const UTIL_A: &str = "use std.bits (i32)\nuse std.int (wrapping_add)\n\n\
    pub fn f(n: i32) -> i32 { wrapping_add(n, 1) }\n\n\
    pub fn h(n: i32) -> i32 { n }\n\n\
    fn secret(n: i32) -> i32 { n }\n";

const UTIL_B: &str = "use std.bits (i32)\nuse std.int (wrapping_mul)\n\n\
    pub fn f(n: i32) -> i32 { wrapping_mul(n, 2) }\n";

/// One mistake, and the error it must give.
#[derive(Debug)]
struct Mistake {
    which: usize,
    /// Blank lines before the first line.
    pad: usize,
}

const KINDS: usize = 8;

/// The file with the mistake, where the error is, and what it mentions.
fn placed(m: &Mistake) -> (String, Pos, &'static str) {
    let pad = "\n".repeat(m.pad);
    let main = |code: &str| format!("fn main() ! console, alloc {{\n{code}  print(\"{{x}}\")\n}}\n");
    let (before, after, mentions): (String, String, &'static str) = match m.which {
        // An alias that the file defines.
        0 => (
            format!("{pad}use geo.util (f as "),
            format!("g)\nuse std.bits (i32)\nuse std.console (console, print)\n\nfn g() -> i32 {{ 1 }}\n\n{}", main("  x = 1\n")),
            "`g`",
        ),
        // An alias that another listed name has.
        1 => (
            format!("{pad}use geo.util (f, h)\nuse std.console (console, print)\nuse ui.util (f as "),
            format!("h)\n\n{}", main("  x = 1\n")),
            "`h`",
        ),
        // Two modules that are written alike.
        2 => (
            format!("{pad}use geo.util\nuse std.console (console, print)\nuse ui."),
            format!("util\n\n{}", main("  x = 1\n")),
            "`util`",
        ),
        // A name written with a module that is not imported.
        3 => (
            format!("{pad}use geo.util (f)\nuse std.console (console, print)\n\nfn main() ! console, alloc {{\n  x = "),
            "ui.f(1)\n  print(\"{x}\")\n}\n".to_string(),
            "`ui`",
        ),
        // A function of two traits, written without the trait.
        4 => (
            format!(
                "{pad}use shapes (S, TA, make, size)\nuse solids (TB)\nuse std.console (console, print)\n\n\
                 fn main() ! console, alloc {{\n  x = "
            ),
            "size(make(1))\n  print(\"{x}\")\n}\n".to_string(),
            "`size`",
        ),
        // A name the module keeps private, written with the module.
        5 => (
            format!("{pad}use geo.util\nuse std.console (console, print)\n\nfn main() ! console, alloc {{\n  x = util."),
            "secret(1)\n  print(\"{x}\")\n}\n".to_string(),
            "`secret`",
        ),
        // A name the module does not have, written with the module.
        6 => (
            format!("{pad}use geo.util\nuse std.console (console, print)\n\nfn main() ! console, alloc {{\n  x = util."),
            "nothing(1)\n  print(\"{x}\")\n}\n".to_string(),
            "`nothing`",
        ),
        // A module written as a name that is listed.
        _ => (
            format!("{pad}use ui.util (f)\nuse std.console (console, print)\nuse geo.util as "),
            format!("f\n\n{}", main("  x = 1\n")),
            "`f`",
        ),
    };
    (format!("{before}{after}"), pos_after(&before), mentions)
}

/// Each of these is one error at its place, which the generator wrote: an
/// alias that is already a name in the file, two modules written alike
/// without `as`, a name written with a module the file does not import, and a
/// call of a function that two traits in scope both have, written without the
/// trait; and a private name and a missing one, written with their module.
fn aliases_each_mistake_is_one_error_at_its_place() {
    generate::check(
        23,
        40,
        |g| Mistake {
            which: g.below(KINDS),
            pad: g.below(4),
        },
        |m| {
            let (main, at, mentions) = placed(m);
            let a = shapes(1);
            let b = solids(2);
            let files = [
                ("geo.util", UTIL_A),
                ("ui.util", UTIL_B),
                ("shapes", a.as_str()),
                ("solids", b.as_str()),
            ];
            let Err(errors) = lower_project(main.as_bytes(), &files) else {
                panic!("accepted:\n{main}");
            };
            assert_eq!(errors.len(), 1, "{errors:?} for\n{main}");
            let e: &Error = &errors[0];
            assert_eq!(e.module, None, "{errors:?}");
            assert_eq!(e.pos, at, "{errors:?} for\n{main}");
            assert!(e.message.contains(mentions), "{e}");
            assert!(!e.message.contains('@'), "{e}");
        },
    );
}

/// The examples of the pages, with the names of the pages, over whole
/// numbers.
fn aliases_the_examples_of_the_pages_give_what_the_pages_say() {
    let transaction = "use std.bits (i32)\nuse std.int (wrapping_add, wrapping_mul)\n\n\
        pub fn begin(n: i32) -> i32 { wrapping_add(n, 1) }\n\n\
        pub fn commit(n: i32) -> i32 { wrapping_mul(n, 3) }\n";
    let animation = "use std.bits (i32)\nuse std.int (wrapping_add)\n\n\
        pub fn begin(n: i32) -> i32 { wrapping_add(n, 100) }\n";
    let files = [("db.transaction", transaction), ("ui.animation", animation)];
    let programs = [
        // `as` on a name.
        (
            "use db.transaction (begin)\nuse std.console (console, print)\nuse ui.animation (begin as start)\n\n\
             fn main() ! console, alloc {\n  t = begin(1)\n  a = start(1)\n  print(\"{t} {a}\")\n}\n",
            "2 101",
        ),
        // A module imported, and a name written with it.
        (
            "use db.transaction\nuse std.console (console, print)\n\n\
             fn main() ! console, alloc {\n  t = transaction.begin(1)\n  \
             c = transaction.commit(t)\n  print(\"{t} {c}\")\n}\n",
            "2 6",
        ),
        // Two modules that share a name, each written with its module.
        (
            "use db.transaction\nuse std.console (console, print)\nuse ui.animation as animation\n\n\
             fn main() ! console, alloc {\n  t = transaction.begin(1)\n  \
             a = animation.begin(1)\n  print(\"{t} {a}\")\n}\n",
            "2 101",
        ),
        // A module under another name.
        (
            "use db.transaction\nuse std.console (console, print)\nuse ui.animation as anim\n\n\
             fn main() ! console, alloc {\n  t = transaction.begin(1)\n  \
             a = anim.begin(1)\n  print(\"{t} {a}\")\n}\n",
            "2 101",
        ),
    ];
    for (source, wanted) in programs {
        let program =
            lower_project(source.as_bytes(), &files).unwrap_or_else(|e| panic!("{e:?}\n{source}"));
        assert_eq!(
            String::from_utf8(printed(&program)).unwrap().trim_end(),
            wanted,
            "for\n{source}"
        );
    }
}
