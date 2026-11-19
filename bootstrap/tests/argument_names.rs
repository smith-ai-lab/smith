//! A call that gives an argument a name that is no parameter of the function it
//! calls is one mistake at the name, however the function is written and
//! whatever it states, and the compiler goes on to report the rest of the
//! program and never stops.

use smith::diag::{Error, Pos};
use smith::{check, lower_project};

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[(
    "a_name_that_is_no_parameter_is_one_error_at_the_name_however_the_function_is_written",
    a_name_that_is_no_parameter_is_one_error_at_the_name_however_the_function_is_written,
)];

/// How the function is written.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Written {
    Plain,
    /// In a module that the file imports, called with the module's name.
    Module,
    /// A function of a trait, called with the trait's name, with another trait
    /// that has a function of the same name beside it.
    Trait,
    /// A function of the library that the compiler reads the arguments of: a
    /// shift, whose amount is proven below the number of bits.
    Library,
}

/// What the function states about its arguments, which a call proves.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum States {
    Nothing,
    /// A `requires` about a number.
    Requires,
    /// An `ensures` about the result.
    Ensures,
    /// A `requires` that names no trait, which is a mistake of its own.
    NoTrait,
    Both,
}

/// The place of `needle` in `source`.
fn place_of(source: &str, needle: &str) -> Pos {
    let at = source.find(needle).unwrap();
    let line = source[..at].matches('\n').count() + 1;
    let col = at - source[..at].rfind('\n').map_or(0, |n| n + 1) + 1;
    Pos { line, col }
}

/// The errors the file gives, with the module `shapes` it imports when it is
/// written in one. A file that is accepted gives none.
fn errors(main: &str, module: Option<&str>) -> Vec<Error> {
    match module {
        None => check(main.as_bytes()).err().unwrap_or_default(),
        Some(module) => lower_project(main.as_bytes(), &[("shapes", module)])
            .err()
            .unwrap_or_default(),
    }
}

fn a_name_that_is_no_parameter_is_one_error_at_the_name_however_the_function_is_written() {
    let written = [Written::Plain, Written::Module, Written::Trait, Written::Library];
    let states = [
        States::Nothing,
        States::Requires,
        States::Ensures,
        States::NoTrait,
        States::Both,
    ];
    generate::check(
        4301,
        60,
        |g: &mut Gen| {
            let letters: String = (0..g.below(4) + 1)
                .map(|_| (b'p' + g.below(10) as u8) as char)
                .collect();
            (
                *g.pick(&written),
                *g.pick(&states),
                g.below(2) == 0,
                format!("{letters}0"),
            )
        },
        |(how, says, first, wrong)| {
            let stated = match says {
                States::Nothing => "",
                States::Requires => "  requires a < 200\n",
                States::Ensures => "  ensures a < 200\n",
                States::NoTrait => "  requires Dicard[T]\n",
                States::Both => "  requires a < 200\n  ensures a < 200\n",
            };
            // The function, with a result named for an `ensures`.
            let result = match says {
                States::Ensures | States::Both => "result: Byte",
                _ => "Byte",
            };
            let function = format!("fn f[T](a: Byte, b: T) -> {result}\n{stated}{{\n  a\n}}\n");
            // The arguments, the wrong name for the first or the second.
            let args = if *first {
                format!("{wrong}: 1, b: 2")
            } else {
                format!("1, {wrong}: 2")
            };
            let shift = if *first {
                format!("{wrong}: x, by: 1")
            } else {
                format!("x, {wrong}: 1")
            };
            let (main, module) = match how {
                Written::Library => (
                    format!("use std.bits (shift_left)\nuse std.console (console, print)\n\nfn main() ! console, alloc {{\n  x: Byte = 3\n  print(\"{{shift_left({shift})}}\")\n}}\n"),
                    None,
                ),
                Written::Plain => (
                    format!("use std.console (console, print)\n\n{function}\nfn main() ! console, alloc {{\n  print(\"{{f({args})}}\")\n}}\n"),
                    None,
                ),
                Written::Module => (
                    format!("use shapes\nuse std.console (console, print)\n\nfn main() ! console, alloc {{\n  print(\"{{shapes.f({args})}}\")\n}}\n"),
                    Some(format!("pub {function}")),
                ),
                Written::Trait => (
                    format!(
                        "use std.console (console, print)\n\ntrait Shown[T] {{\n  fn f(a: Byte, b: T) -> Byte\n}}\n\ntrait Named[T] {{\n  fn f(a: Byte, b: T) -> Byte\n}}\n\nimpl Shown[Byte] {{\n  fn f(a: Byte, b: Byte) -> Byte {{ a }}\n}}\n\nimpl Named[Byte] {{\n  fn f(a: Byte, b: Byte) -> Byte {{ a }}\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{Named.f({args})}}\")\n}}\n"
                    ),
                    None,
                ),
            };
            let found = errors(&main, module.as_deref());
            let at = place_of(&main, &format!("{wrong}:"));
            let named: Vec<&Error> = found
                .iter()
                .filter(|e| e.message.contains("no parameter named") && e.module.is_none())
                .collect();
            assert_eq!(named.len(), 1, "{found:?} for\n{main}");
            assert_eq!(named[0].pos, at, "{found:?} for\n{main}");
            assert!(named[0].message.contains(wrong.as_str()), "{found:?} for\n{main}");
        },
    );
}
