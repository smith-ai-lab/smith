//! A function type is written `fn(T) -> R`, and a lambda may write the types
//! of its parameters and of its result.

use std::path::Path;

use smith::diag::Error;
use smith::lower;
use smith::scalar;

use crate::Test;
use crate::generate::{self, Gen};
use crate::lambdas;
use crate::programs::pos_after;

pub const ONCE: &[Test] = &[
    (
        "a_lambda_does_the_same_whichever_of_its_types_it_writes",
        a_lambda_does_the_same_whichever_of_its_types_it_writes,
    ),
    (
        "writing_the_types_of_a_generated_lambda_builds_the_same_program",
        writing_the_types_of_a_generated_lambda_builds_the_same_program,
    ),
    (
        "a_type_a_lambda_writes_that_is_not_the_one_needed_is_one_error_at_the_type",
        a_type_a_lambda_writes_that_is_not_the_one_needed_is_one_error_at_the_type,
    ),
    (
        "a_lambda_whose_types_nothing_decides_is_one_error_at_the_lambda_until_it_writes_them",
        a_lambda_whose_types_nothing_decides_is_one_error_at_the_lambda_until_it_writes_them,
    ),
    (
        "the_arrow_form_of_a_type_is_one_error_at_its_place_whose_fix_is_the_fn_form",
        the_arrow_form_of_a_type_is_one_error_at_its_place_whose_fix_is_the_fn_form,
    ),
    (
        "types_in_parentheses_are_a_tuple_and_fn_with_them_is_a_function_wherever_a_type_is_written",
        types_in_parentheses_are_a_tuple_and_fn_with_them_is_a_function_wherever_a_type_is_written,
    ),
    (
        "no_file_of_smith_and_no_page_of_knowledge_writes_the_arrow_form",
        no_file_of_smith_and_no_page_of_knowledge_writes_the_arrow_form,
    ),
];

/// The whole number types.
const INTS: [&str; 8] = ["i8", "i16", "i32", "Int", "Byte", "u16", "u32", "u64"];

/// The `use` line that imports the sizes among `types`, and the blank line
/// after it, or nothing when none is a size: `Int` and `Byte` are the prelude's.
fn sizes(types: &[&str]) -> String {
    let mut names: Vec<&str> = types
        .iter()
        .copied()
        .filter(|t| !["Int", "Byte", "bool", "str"].contains(t))
        .collect();
    names.sort();
    names.dedup();
    if names.is_empty() {
        String::new()
    } else {
        format!("use std.bits ({})\n\n", names.join(", "))
    }
}

/// The `use` lines of a program that prints and writes the sizes among `types`.
fn bits_and_console(types: &[&str]) -> String {
    let bits = sizes(types);
    let bits = bits.strip_suffix('\n').unwrap_or("");
    format!("{bits}use std.console (console, print)\n\n")
}

fn printed(source: &str) -> Vec<u8> {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} in {source}"));
    scalar::run(&program).stdout
}

/// `twice(3)` with a lambda that writes each of its types, or none.
fn a_lambda_does_the_same_whichever_of_its_types_it_writes() {
    for lambda in [
        "fn(v) { wrapping_mul(v, 2) }",
        "fn(v: u64) { wrapping_mul(v, 2) }",
        "fn(v) -> u64 { wrapping_mul(v, 2) }",
        "fn(v: u64) -> u64 { wrapping_mul(v, 2) }",
    ] {
        let source = format!(
            "use std.bits (u64)\nuse std.console (console, print)\nuse std.int (wrapping_mul)\n\n\
             fn twice(x: u64, f: fn(u64) -> u64) -> u64 {{ f(f(x)) }}\n\n\
             fn main() ! console, alloc {{\n  r = twice(3) {lambda}\n  print(\"{{r}}\")\n}}\n"
        );
        assert_eq!(printed(&source), b"12\n", "{lambda}");
    }
}

/// Every program the generator builds that passes a lambda is the same
/// program when each lambda writes the types of its parameters, of its
/// result, or both.
fn writing_the_types_of_a_generated_lambda_builds_the_same_program() {
    generate::check(201, 8, lambdas::program, |case| {
        let program = lower(case.source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
        for typed in &case.typed {
            let written = lower(typed.as_bytes()).unwrap_or_else(|e| panic!("{e:?} in {typed}"));
            assert!(program == written, "differs from\n{typed}");
        }
    });
}

/// The one error `source` gives.
fn one_error(source: &str) -> Error {
    let found = lower(source.as_bytes()).expect_err("an error");
    assert_eq!(found.len(), 1, "{found:?} in {source}");
    found.into_iter().next().expect("one")
}

/// A type written for a lambda's parameter or result that is not the type
/// of the function it is passed as is one error, at the type, which names
/// both.
fn a_type_a_lambda_writes_that_is_not_the_one_needed_is_one_error_at_the_type() {
    let wrong = |g: &mut Gen| {
        let needed = *g.pick(&INTS);
        let others: Vec<&str> = INTS
            .iter()
            .copied()
            .chain(["bool", "str"])
            .filter(|t| *t != needed)
            .collect();
        (needed, *g.pick(&others), g.below(2) == 0)
    };
    generate::check(202, 24, wrong, |&(needed, written, in_parameter)| {
        let uses = sizes(&[needed, written]);
        let head =
            format!("{uses}fn apply(f: fn({needed}) -> {needed}) -> {needed} {{ f(1) }}\n\n");
        let (before, lambda) = if in_parameter {
            ("fn main() {\n  r = apply fn(v: ", format!("{written}) {{ v }}"))
        } else {
            ("fn main() {\n  r = apply fn(v) -> ", format!("{written} {{ v }}"))
        };
        let source = format!("{head}{before}{lambda}\n}}\n");
        let error = one_error(&source);
        assert_eq!(error.pos, pos_after(&format!("{head}{before}")), "{error}");
        for name in [needed, written] {
            // The checker calls `str` text.
            let name = if name == "str" { "text" } else { name };
            assert!(error.message.contains(name), "{error} lacks {name}");
        }
    });
}

/// A lambda passed where nothing decides its parameter's type is one error at
/// the lambda, which says to write the type, and it builds once it does.
fn a_lambda_whose_types_nothing_decides_is_one_error_at_the_lambda_until_it_writes_them() {
    generate::check(
        203,
        8,
        |g| *g.pick(&INTS),
        |&ty| {
            let head = format!(
                "{}fn pick[T](f: fn(T) -> u64) -> u64 {{ 1 }}\n\nfn main() {{\n  r = pick ",
                sizes(&["u64", ty])
            );
            let error = one_error(&format!("{head}fn(v) {{ 2 }}\n}}\n"));
            assert_eq!(error.pos, pos_after(&head), "{error}");
            assert!(error.message.contains("write"), "{error}");
            let written = format!("{head}fn(v: {ty}) {{ 2 }}\n}}\n");
            lower(written.as_bytes()).unwrap_or_else(|e| panic!("{e:?} in {written}"));
        },
    );
}

/// The places a type is written, as the text before it and the text after.
const PLACES: [(&str, &str); 5] = [
    ("fn f(g: ", ") {}\n"),
    ("fn f() -> ", " { f() }\n"),
    ("struct S {\n  g: ", "\n}\n"),
    ("fn f() {\n  g: ", " = h\n}\n"),
    ("fn f() {\n  g: List[", "] = []\n}\n"),
];

/// `(A) -> R` written as a type is one error at its place, whose fix is
/// `fn(A) -> R`.
fn the_arrow_form_of_a_type_is_one_error_at_its_place_whose_fix_is_the_fn_form() {
    let types = |g: &mut Gen| (*g.pick(&INTS), *g.pick(&INTS), g.below(PLACES.len()));
    generate::check(204, 30, types, |&(param, result, place)| {
        let (before, after) = PLACES[place];
        let before = format!("{}{before}", sizes(&[param, result]));
        let source = format!("{before}({param}) -> {result}{after}");
        let error = one_error(&source);
        assert_eq!(error.pos, pos_after(&before), "{error}");
        let fix = error.fix.as_ref().expect("a fix");
        assert!(fix.contains(&format!("fn({param}) -> {result}")), "{fix}");
    });
}

/// `(A, B)` is a tuple and `fn(A, B)` is a function that gives nothing, in a
/// parameter, a result, a field, a binding, and a type argument.
fn types_in_parentheses_are_a_tuple_and_fn_with_them_is_a_function_wherever_a_type_is_written() {
    let types = |g: &mut Gen| (*g.pick(&INTS), *g.pick(&INTS));
    generate::check(205, 8, types, |&(a, b)| {
        let tuple = format!("({a}, {b})");
        let uses = sizes(&[a, b]);
        let source = format!(
            "{uses}struct S {{\n  t: {tuple}\n}}\n\
             fn make(t: {tuple}) -> {tuple} {{ t }}\n\n\
             fn main() ! alloc {{\n  s = S(t: make((1, 2)))\n  u: {tuple} = s.t\n  \
             xs: List[{tuple}] = [u]\n}}\n"
        );
        lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} in {source}"));
        // The function gives nothing, so it is called for what it does.
        let source = format!(
            "{}fn run(f: fn({a}, {b})) ! console {{ f(1, 2) }}\n\n\
             fn main() ! console, alloc {{\n  run fn(x, y) {{ print(\"{{x}}\") }}\n}}\n",
            bits_and_console(&[a, b])
        );
        lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} in {source}"));
        // It is a function in every place, though only a parameter's type is built.
        for (before, after, tail) in REACHED {
            let (before, tail) = (
                before.replace("@A", a).replace("@B", b),
                tail.replace("@A", a).replace("@B", b),
            );
            let before = format!("{uses}{before}");
            let source = format!("{before}fn({a}, {b}){after}{tail}");
            let found = lower(source.as_bytes()).expect_err("an error");
            assert!(found.iter().all(|e| e.unbuilt), "{found:?} in {source}");
            let at = pos_after(&before);
            assert!(found.iter().any(|e| e.pos == at), "{found:?}");
        }
    });
}

/// The places a type is written that are not a parameter, with a `main` that
/// reaches each.
const REACHED: [(&str, &str, &str); 4] = [
    ("fn f() -> ", " { f() }\n", "\nfn main() { f() }\n"),
    (
        "struct S {\n  g: ",
        "\n}\n",
        "\nfn f(a: @A, b: @B) {}\n\nfn main() {\n  s = S(g: f)\n}\n",
    ),
    ("fn h(a: @A, b: @B) {}\n\nfn main() {\n  g: ", " = h\n}\n", ""),
    ("fn main() ! alloc {\n  g: List[", "] = []\n}\n", ""),
];

/// Whether `text` writes a function type as `(T) -> R`: a list of types in
/// parentheses, where a name is not called, followed by an arrow.
fn writes_the_arrow_form(text: &str) -> bool {
    let mut open = Vec::new();
    for (at, c) in text.char_indices() {
        match c {
            '(' => open.push(at),
            ')' => {
                let Some(start) = open.pop() else { continue };
                if !text[at + 1..].trim_start_matches([' ', '\t']).starts_with("->") {
                    continue;
                }
                let before = text[..start].trim_end_matches([' ', '\t']);
                let called = before
                    .chars()
                    .next_back()
                    .is_some_and(|c| c.is_alphanumeric() || c == '_' || c == ']');
                if !called {
                    return true;
                }
            }
            _ => {}
        }
    }
    false
}

/// The files under `dir` with the extension `ext`, but not those a build
/// or a checkout writes.
fn files(dir: &Path, ext: &str, found: &mut Vec<std::path::PathBuf>) {
    for entry in std::fs::read_dir(dir).expect("a folder") {
        let path = entry.expect("an entry").path();
        let name = path.file_name().and_then(|n| n.to_str()).unwrap_or("");
        if path.is_dir() {
            if !matches!(name, "target" | ".git") {
                files(&path, ext, found);
            }
        } else if path.extension().is_some_and(|e| e == ext) {
            found.push(path);
        }
    }
}

/// No `.smith` file of the repository and no page of `knowledge/` writes a
/// function type as `(T) -> R`.
fn no_file_of_smith_and_no_page_of_knowledge_writes_the_arrow_form() {
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("..");
    let mut found = Vec::new();
    files(&root, "smith", &mut found);
    files(&root.join("knowledge"), "md", &mut found);
    assert!(found.len() > 100, "{} files", found.len());
    for path in found {
        let text = std::fs::read_to_string(&path).expect("a text file");
        assert!(!writes_the_arrow_form(&text), "{} writes it", path.display());
    }
}
