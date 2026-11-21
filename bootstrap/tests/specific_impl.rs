//! Of two impls that both fit a use, the one for fewer types is used, chosen
//! at the concrete type and not inside a generic function. Two that fit and
//! neither stands for the other are refused.

use smith::scalar;
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "of_the_impls_that_fit_a_use_the_one_for_the_fewest_types_is_the_one_used",
        of_the_impls_that_fit_a_use_the_one_for_the_fewest_types_is_the_one_used,
    ),
    (
        "a_generic_function_uses_the_impl_of_the_type_it_is_called_with",
        a_generic_function_uses_the_impl_of_the_type_it_is_called_with,
    ),
    (
        "two_impls_that_fit_with_neither_for_fewer_types_are_refused",
        two_impls_that_fit_with_neither_for_fewer_types_are_refused,
    ),
];

fn printed(source: &str) -> String {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
    String::from_utf8(scalar::run(&program).stdout).expect("text")
}

/// The impls a generated program writes, from the most general to the most
/// specific of a chain: `T`, `P`, `List[T]`, and `List[P]`.
const IMPLS: [(&str, &str); 4] = [
    ("[T] R[T]", "any"),
    ("R[P]", "p"),
    ("[T] R[List[T]]", "list"),
    ("R[List[P]]", "list of p"),
];

/// The impls, by their place in `IMPLS`, that fit each type that is asked
/// about, from the most general: `P`, `str`, `List[str]`, and `List[P]`.
const FITS: [(&str, &[usize]); 4] = [
    ("P(a: \"a\")", &[0, 1]),
    ("\"a\"", &[0]),
    ("strs", &[0, 2]),
    ("ps", &[0, 2, 3]),
];

fn of_the_impls_that_fit_a_use_the_one_for_the_fewest_types_is_the_one_used() {
    let sets = |g: &mut Gen| (0..4).map(|_| g.one_in(2)).collect::<Vec<bool>>();
    generate::check(8401, 6, sets, |written| {
        let mut source = String::from(
            "use std.console (console, print)\n\ntrait R[T] {\n  fn r(x: T) -> str\n}\n\nstruct P {\n  a: str\n}\n\n",
        );
        for (i, (head, said)) in IMPLS.iter().enumerate() {
            if written[i] {
                let (generics, rest) = match head.strip_prefix("[T] ") {
                    Some(rest) => ("[T]", rest),
                    None => ("", *head),
                };
                let ty = &rest[2..rest.len() - 1];
                source += &format!(
                    "impl{generics} R[{ty}] {{\n  fn r(x: {ty}) -> str {{ \"{said}\" }}\n}}\n\n"
                );
            }
        }
        source += "fn main() ! console, alloc {\n  strs: List[str] = []\n  ps: List[P] = []\n";
        let mut expected = String::new();
        for (value, fits) in FITS {
            // The impl for fewest types is the last of the chain that is written.
            let used = fits.iter().rev().find(|&&i| written[i]);
            if let Some(&i) = used {
                source += &format!("  print(R[{}].r({value}))\n", ty_of(value));
                expected += &format!("{}\n", IMPLS[i].1);
            }
        }
        source += "}\n";
        assert_eq!(printed(&source), expected, "for\n{source}");
    });
}

fn ty_of(value: &str) -> &'static str {
    match value {
        "strs" => "List[str]",
        "ps" => "List[P]",
        "\"a\"" => "str",
        _ => "P",
    }
}

fn a_generic_function_uses_the_impl_of_the_type_it_is_called_with() {
    let source = "use std.console (console, print)\nuse std.serialize (Serialize)\n\n\
trait Respond[T] {\n  fn respond(value: T) -> str\n}\n\n\
impl[T] Respond[T]\n  requires Serialize[T]\n{\n  fn respond(value: T) -> str { \"json\" }\n}\n\n\
struct Page {\n  title: str\n}\n\nstruct Plain {\n  title: str\n}\n\n\
impl Respond[Page] {\n  fn respond(value: Page) -> str { \"page\" }\n}\n\n\
fn send[T](x: T) -> str\n  requires Serialize[T]\n{\n  Respond[T].respond(x)\n}\n\n\
fn main() ! console {\n  print(send(Page(title: \"a\")))\n  print(send(Plain(title: \"a\")))\n}\n";
    assert_eq!(printed(source), "page\njson\n");
}

fn two_impls_that_fit_with_neither_for_fewer_types_are_refused() {
    let source = "use std.console (console, print)\n\ntrait R[T] {\n  fn r(x: T) -> str\n}\n\nstruct P {\n  a: str\n}\n\n\
impl[T] R[(T, P)] {\n  fn r(x: (T, P)) -> str { \"first\" }\n}\n\n\
impl[U] R[(P, U)] {\n  fn r(x: (P, U)) -> str { \"second\" }\n}\n\n\
fn main() ! console {\n  print(R[(P, P)].r((P(a: \"a\"), P(a: \"b\"))))\n}\n";
    let errors = check(source.as_bytes()).expect_err("refused");
    assert!(
        errors.iter().any(|e| e.message.contains("already implemented")),
        "{errors:?}"
    );
}
