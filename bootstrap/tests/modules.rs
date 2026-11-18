//! A program in several files: each file is a module named by its path, a name
//! is private unless it is `pub`, and `use` brings a module's names into
//! another. What a tree of modules prints, what is said of each mistake in one,
//! and that a file nothing reaches is never read by a build.

use std::path::Path;

use smith::diag::{Error, Pos};
use smith::{Project, check_module, lower, lower_project};

use crate::Test;
use crate::generate::{self, Gen};
use crate::programs::pos_after;
use crate::targets::any;

pub const ONCE: &[Test] = &[
    (
        "project_modules_print_what_the_same_items_print_in_one_file",
        project_modules_print_what_the_same_items_print_in_one_file,
    ),
    (
        "project_each_mistake_in_a_tree_of_modules_is_one_error_at_its_place",
        project_each_mistake_in_a_tree_of_modules_is_one_error_at_its_place,
    ),
    (
        "project_a_file_nothing_reaches_is_never_read_by_a_build_and_is_checked",
        project_a_file_nothing_reaches_is_never_read_by_a_build_and_is_checked,
    ),
    (
        "project_a_build_in_a_folder_ignores_a_wrong_file_and_check_reports_it",
        project_a_build_in_a_folder_ignores_a_wrong_file_and_check_reports_it,
    ),
];

/// The tests that run a program, whose result depends on the kind of machine.
pub const MACHINE: &[Test] = &[(
    "project_run_in_a_folder_runs_a_program_that_imports_from_its_files",
    project_run_in_a_folder_runs_a_program_that_imports_from_its_files,
)];

/// What a program of the scalar form prints.
fn printed(program: &smith::scalar::Program) -> Vec<u8> {
    smith::scalar::run(program).stdout
}

/// The names a module may have, each a path of folders and a file.
const NAMES: &[&str] = &["geo", "shapes.circle", "util.math", "store.cart.line", "zeta"];

/// A tree of modules, each of which may import from one before it, and a main
/// that imports from every one.
#[derive(Debug)]
struct Tree {
    /// The module's name, and the earlier module it imports from, if any.
    modules: Vec<(&'static str, Option<usize>)>,
    /// The numbers the items hold, four for each module.
    numbers: Vec<[i32; 4]>,
    /// What the main passes.
    args: [i32; 3],
}

fn tree(g: &mut Gen) -> Tree {
    let mut names: Vec<&'static str> = NAMES.to_vec();
    let count = 1 + g.below(NAMES.len());
    let mut modules = Vec::new();
    for i in 0..count {
        let name = names.remove(g.below(names.len()));
        let from = (i > 0 && !g.one_in(3)).then(|| g.below(i));
        modules.push((name, from));
    }
    let number = |g: &mut Gen| g.below(90) as i32 + 1;
    let numbers = modules
        .iter()
        .map(|_| [number(g), number(g), number(g), number(g)])
        .collect();
    Tree {
        modules,
        numbers,
        args: [number(g), number(g), number(g)],
    }
}

/// The items of the module `i`. Written as a file of the module, each item
/// that others use is `pub`, the struct has a private field, and the module
/// has a private function that every module names alike. Written for one file,
/// nothing is `pub` and the private function is named for its module.
fn items(t: &Tree, i: usize, separate: bool) -> String {
    let (p, helper) = if separate {
        ("pub ", "helper".to_string())
    } else {
        ("", format!("helper{i}"))
    };
    let [k0, k1, k2, k3] = t.numbers[i];
    let mut text = String::new();
    if separate && let Some(j) = t.modules[i].1 {
        text += &format!(
            "use {} (S{j}, f{j}, make{j}, sum{j})\n\n",
            t.modules[j].0
        );
    }
    text += &format!(
        "fn {helper}(n: i32) -> i32 {{ wrapping_mul(n, {k0}) }}\n\n\
         {p}fn f{i}(n: i32) -> i32 {{ wrapping_add({helper}(n), {k1}) }}\n\n\
         {p}struct S{i} {{\n  {p}a: i32\n  b: i32\n}}\n\n\
         {p}fn make{i}(x: i32) -> S{i} {{ S{i}(a: x, b: f{i}(x)) }}\n\n\
         {p}fn sum{i}(s: S{i}) -> i32 {{ wrapping_add(s.a, s.b) }}\n\n\
         {p}enum E{i} {{\n  one\n  two: i32\n}}\n\n\
         {p}fn val{i}(e: E{i}) -> i32 {{\n  match e {{\n    .one {{ {k2} }}\n    .two(n) {{ f{i}(n) }}\n  }}\n}}\n\n\
         {p}trait T{i}[X] {{\n  fn t{i}(x: X) -> i32\n}}\n\n\
         impl T{i}[S{i}] {{\n  fn t{i}(x: S{i}) -> i32 {{ sum{i}(x) }}\n}}\n\n"
    );
    match t.modules[i].1 {
        Some(j) => {
            text += &format!(
                "impl T{i}[S{j}] {{\n  fn t{i}(x: S{j}) -> i32 {{ wrapping_add(sum{j}(x), f{j}({k3})) }}\n}}\n\n\
                 {p}fn mix{i}(n: i32) -> i32 {{ wrapping_add(f{j}(n), sum{j}(make{j}(n))) }}\n\n"
            );
        }
        None => {
            text += &format!("{p}fn mix{i}(n: i32) -> i32 {{ f{i}(n) }}\n\n");
        }
    }
    text
}

/// The main: the `use` lines of every module, and a function that prints what
/// each module's items give.
fn main_text(t: &Tree, with_uses: bool) -> String {
    let mut text = String::new();
    if with_uses {
        for (i, (name, _)) in t.modules.iter().enumerate() {
            text += &format!("use {name} (f{i}, make{i}, mix{i}, sum{i}, t{i}, val{i})\n");
        }
        text += "\n";
    }
    let [a, b, c] = t.args;
    text += "fn helper(n: i32) -> i32 { wrapping_add(n, 7) }\n\nfn main() ! console, alloc {\n";
    for i in 0..t.modules.len() {
        text += &format!(
            "  print(\"{{f{i}({a})}} {{sum{i}(make{i}({b}))}} {{make{i}({c}).a}} {{val{i}(.two({a}))}} {{val{i}(.one)}} {{t{i}(make{i}({b}))}} {{mix{i}({c})}} {{helper({a})}}\")\n"
        );
    }
    text + "}\n"
}

/// A tree of modules, each in a file of its own, prints what the same items
/// print in one file. The modules share the name of a private function, each
/// keeps its field private, an `impl` is found where the main never names it,
/// and a module imports from another.
fn project_modules_print_what_the_same_items_print_in_one_file() {
    generate::check(5, 40, tree, |t| {
        let modules: Vec<String> = (0..t.modules.len()).map(|i| items(t, i, true)).collect();
        let files: Vec<(&str, &str)> = t
            .modules
            .iter()
            .zip(&modules)
            .map(|((name, _), text)| (*name, text.as_str()))
            .collect();
        let main = main_text(t, true);
        let got = lower_project(main.as_bytes(), &files)
            .unwrap_or_else(|e| panic!("{e:?}\n{main}\n{files:?}"));
        let one: String = (0..t.modules.len())
            .map(|i| items(t, i, false))
            .chain([main_text(t, false)])
            .collect();
        let wanted = lower(one.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{one}"));
        assert_eq!(printed(&got), printed(&wanted), "for\n{main}\n{files:?}");
        assert!(!printed(&got).is_empty());
    });
}

/// One mistake in a tree of two modules, where `util.math` imports from `geo`
/// and the main imports from both.
#[derive(Debug)]
struct Mistake {
    which: usize,
    /// Blank lines before the file the mistake is in.
    pad: usize,
}

const KINDS: usize = 8;

const GEO: &str = "pub fn f0(n: i32) -> i32 { wrapping_add(n, 1) }\n\n\
    pub struct S0 {\n  pub a: i32\n  b: i32\n}\n\n\
    pub fn make0(x: i32) -> S0 { S0(a: x, b: 2) }\n\n\
    fn secret(n: i32) -> i32 { n }\n";

const MATH: &str = "use geo (S0, f0)\n\n\
    pub trait T1[X] {\n  fn t1(x: X) -> i32\n}\n\n\
    impl T1[S0] {\n  fn t1(x: S0) -> i32 { f0(x.a) }\n}\n";

/// The error a mistake must give: the module it is in, the place, and what it
/// says.
struct Placed {
    main: String,
    modules: Vec<(&'static str, String)>,
    module: Option<&'static str>,
    at: Pos,
    mentions: &'static str,
}

fn placed(m: &Mistake) -> Placed {
    let pad = "\n".repeat(m.pad);
    let head = "use geo (S0, make0)\nuse util.math (T1, t1)\n\n";
    let body = |code: &str| format!("fn main() ! console {{\n{code}  print(\"x\")\n}}\n");
    let plain = |main: String, at: Pos, mentions: &'static str| Placed {
        main,
        modules: vec![("geo", GEO.to_string()), ("util.math", MATH.to_string())],
        module: None,
        at,
        mentions,
    };
    match m.which {
        0 => {
            let before = format!("{pad}use geo (make0)\nuse ");
            let main = format!("{before}ghost.deep (x)\n\n{}", body(""));
            plain(main, pos_after(&before), "ghost.deep")
        }
        1 => {
            let before = format!("{pad}use geo (make0, ");
            let main = format!("{before}secret)\n\n{}", body(""));
            plain(main, pos_after(&before), "secret")
        }
        2 => {
            let before = format!("{pad}use geo (");
            let main = format!("{before}nothing_there)\n\n{}", body(""));
            plain(main, pos_after(&before), "nothing_there")
        }
        3 => {
            // `geo` and `util.math` import each other, so no order builds them.
            let before = "use ";
            let geo = format!("use util.math (t1)\n\n{GEO}");
            let math = format!("{pad}{before}geo (f0)\n\npub fn t1(n: i32) -> i32 {{ f0(n) }}\n");
            Placed {
                main: format!("use geo (f0)\n\n{}", body("")),
                modules: vec![("geo", geo), ("util.math", math)],
                module: Some("util/math.smith"),
                at: pos_after(&format!("{pad}{before}")),
                mentions: "each other",
            }
        }
        4 => {
            let before = format!("{pad}{head}{}", "fn main() ! console {\n  n = make0(1).");
            let main = format!("{before}b\n  print(\"x\")\n}}\n");
            plain(main, pos_after(&before), "`b`")
        }
        5 => {
            let before = format!("{pad}{head}fn main() ! console {{\n  s = ");
            let main = format!("{before}S0(a: 1, b: 2)\n  print(\"x\")\n}}\n");
            plain(main, pos_after(&before), "S0")
        }
        6 => {
            let before = format!("{pad}{head}fn main() ! console {{\n  var s = make0(1)\n  s.");
            let main = format!("{before}b = 3\n  print(\"x\")\n}}\n");
            plain(main, pos_after(&before), "`b`")
        }
        _ => {
            // An `impl` in a module that defines neither its trait nor its type.
            let before = format!("{pad}{head}impl ");
            let main = format!(
                "{before}T1[S0] {{\n  fn t1(x: S0) -> i32 {{ 0 }}\n}}\n\n{}",
                body("")
            );
            plain(main, pos_after(&before), "T1")
        }
    }
}

/// Each of these is one error at its place, and the place is the one the
/// generator wrote: a `use` of a file that does not exist, of a name that is
/// not `pub`, and of a name the module does not define; two modules that
/// import each other; a private field read, built, and assigned outside its
/// module; and an `impl` in a module of neither its trait nor its type.
fn project_each_mistake_in_a_tree_of_modules_is_one_error_at_its_place() {
    generate::check(
        6,
        40,
        |g| Mistake {
            which: g.below(KINDS),
            pad: g.below(4),
        },
        |m| {
            let w = placed(m);
            let files: Vec<(&str, &str)> = w.modules.iter().map(|(n, s)| (*n, s.as_str())).collect();
            let Err(errors) = lower_project(w.main.as_bytes(), &files) else {
                panic!("accepted:\n{}", w.main);
            };
            assert_eq!(errors.len(), 1, "{errors:?} for\n{}", w.main);
            let e: &Error = &errors[0];
            assert_eq!(e.module.as_deref(), w.module, "{errors:?}");
            assert_eq!(e.pos, w.at, "{errors:?} for\n{}", w.main);
            assert!(e.message.contains(w.mentions), "{e}");
            assert!(!e.message.contains('@'), "{e}");
        },
    );
}

/// A file that no `use` reaches is never read by a build, so an error in it
/// stops nothing, and reading it reports the error at its place.
fn project_a_file_nothing_reaches_is_never_read_by_a_build_and_is_checked() {
    generate::check(
        7,
        20,
        |g| (g.below(4), g.below(5)),
        |&(pad, kind)| {
            let bad = [
                "fn broken( {{{\n",
                "use ghost (x)\n",
                "use geo (nothing_there)\n",
                "pub struct S {\n  pub x: i32\n",
                "use geo (f0)\nuse geo (f0)\n",
            ][kind];
            let wrong = format!("{}{bad}", "\n".repeat(pad));
            let main = "use geo (f0)\n\nfn main() ! console, alloc {\n  print(\"{f0(1)}\")\n}\n";
            let reached = [("geo", GEO)];
            let with = [("geo", GEO), ("extra.stray", wrong.as_str())];
            assert_eq!(
                printed(&lower_project(main.as_bytes(), &with).unwrap()),
                printed(&lower_project(main.as_bytes(), &reached).unwrap())
            );
            let files = |name: &str| {
                with.iter()
                    .find(|(n, _)| *n == name)
                    .map(|(_, s)| s.to_string())
            };
            let project = Project {
                files: &files,
                entry: None,
            };
            let found = check_module("extra.stray", &project);
            assert!(!found.is_empty(), "nothing is said of\n{wrong}");
            assert!(
                found
                    .iter()
                    .all(|e| e.module.as_deref() == Some("extra/stray.smith")),
                "{found:?}"
            );
        },
    );
}

/// A folder of files, written from a list of paths and texts.
fn write_project(name: &str, files: &[(&str, &str)]) -> std::path::PathBuf {
    let dir = crate::cli::folder(name);
    for (path, text) in files {
        let file = dir.join(path);
        std::fs::create_dir_all(file.parent().unwrap()).unwrap();
        std::fs::write(file, text).unwrap();
    }
    dir
}

const MAIN_ENTRY: &str = "use db.transaction (Transaction, begin, commit)\n\n\
    fn main() ! console {\n  t = begin(41)\n  if commit(t) == 42 { print(\"committed\") } else { print(\"lost\") }\n}\n";

const TRANSACTION: &str = "pub struct Transaction {\n  id: i32\n}\n\n\
    pub fn begin(n: i32) -> Transaction { Transaction(id: wrapping_add(n, 1)) }\n\n\
    pub fn commit(t: Transaction) -> i32 { t.id }\n";

/// A folder with `main.smith` and `db/transaction.smith`, where the main
/// imports `Transaction`, `begin`, and `commit`, runs with `smith run`.
fn project_run_in_a_folder_runs_a_program_that_imports_from_its_files() {
    let dir = write_project(
        "project-run",
        &[("main.smith", MAIN_ENTRY), ("db/transaction.smith", TRANSACTION)],
    );
    let ran = crate::cli::smith(&dir, &["run"]);
    assert!(ran.status.success(), "{}", crate::cli::stderr(&ran));
    assert_eq!(ran.stdout, b"committed\n");
}

/// A build in a folder reads the files its `use` lines name and no other, so a
/// wrong file beside them stops nothing, and `smith check` says what is wrong
/// in it, naming its path.
fn project_a_build_in_a_folder_ignores_a_wrong_file_and_check_reports_it() {
    let dir = write_project(
        "project-stray",
        &[
            ("main.smith", MAIN_ENTRY),
            ("db/transaction.smith", TRANSACTION),
            ("tools/stray.smith", "fn broken( {{{\n"),
        ],
    );
    let built = crate::cli::smith(&dir, &["build", "--target", any().name()]);
    assert!(built.status.success(), "{}", crate::cli::stderr(&built));
    assert!(!Path::new(&dir.join("tools/stray")).exists());
    let checked = crate::cli::smith(&dir, &["check"]);
    assert!(!checked.status.success());
    let said = crate::cli::stderr(&checked);
    assert!(said.contains("tools/stray.smith:1:"), "{said}");
    assert!(!said.contains("db/transaction.smith"), "{said}");
}
