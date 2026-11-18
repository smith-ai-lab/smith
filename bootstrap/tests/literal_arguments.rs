//! A whole number written as the argument of a parameter whose type is a type
//! parameter, which nothing else decides, is an `i64`.

use smith::diag::Pos;
use smith::lower;
use smith::scalar;

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "a_literal_for_a_type_parameter_is_an_i64_as_a_binding_of_it_is",
        a_literal_for_a_type_parameter_is_an_i64_as_a_binding_of_it_is,
    ),
    (
        "a_literal_for_a_type_parameter_that_needs_a_trait_compiles_when_i64_has_it",
        a_literal_for_a_type_parameter_that_needs_a_trait_compiles_when_i64_has_it,
    ),
    (
        "two_literals_for_the_functions_of_a_trait_with_an_out_type_are_i64",
        two_literals_for_the_functions_of_a_trait_with_an_out_type_are_i64,
    ),
    (
        "a_literal_among_any_arguments_never_panics_the_compiler",
        a_literal_among_any_arguments_never_panics_the_compiler,
    ),
];

/// The values a literal may stand for, which an `i32` cannot all hold.
const VALUES: [i128; 9] = [
    0,
    1,
    255,
    70_000,
    -5,
    5_000_000_000,
    i64::MAX as i128,
    i64::MIN as i128,
    -1,
];

const NAMES: [&str; 4] = ["A", "B", "C", "D"];
const PARAMS: [&str; 4] = ["a", "b", "c", "d"];

/// How an argument is written.
#[derive(Clone, Copy, Debug)]
enum Written {
    /// The number, in decimal.
    Decimal,
    /// The number in hexadecimal, when it is not negative.
    Hex,
    /// The number as the argument of an identity function.
    Passed,
    /// A name bound to the number with its type written, which is an `i64`.
    Bound,
}

fn literal(value: i128, how: Written) -> String {
    match how {
        Written::Hex if value >= 0 => format!("{value:#x}"),
        _ => value.to_string(),
    }
}

/// A function with type parameters, and a call whose arguments are numbers.
#[derive(Debug)]
struct Call {
    /// How many parameters the function has, each of a type parameter of
    /// its own.
    params: usize,
    /// The parameter the function gives back, or that it needs a trait of.
    chosen: usize,
    values: Vec<i128>,
    written: Vec<Written>,
}

fn call(g: &mut Gen) -> Call {
    let params = 1 + g.below(4);
    let values: Vec<i128> = (0..params).map(|_| *g.pick(&VALUES)).collect();
    let written = (0..params)
        .map(|_| *g.pick(&[Written::Decimal, Written::Hex, Written::Passed, Written::Bound]))
        .collect();
    Call {
        params,
        chosen: g.below(params),
        values,
        written,
    }
}

impl Call {
    fn signature(&self) -> String {
        let names = NAMES[..self.params].join(", ");
        let params: Vec<String> = (0..self.params)
            .map(|i| format!("{}: {}", PARAMS[i], NAMES[i]))
            .collect();
        format!("[{names}]({})", params.join(", "))
    }

    /// The statements that bind the names, and the arguments, with each
    /// literal written as `literal` says to write it.
    fn arguments(&self, literals: bool) -> (String, String) {
        let mut binds = String::new();
        let mut args = Vec::new();
        for i in 0..self.params {
            let how = if literals {
                self.written[i]
            } else {
                Written::Bound
            };
            let text = literal(self.values[i], how);
            args.push(match how {
                Written::Bound => {
                    binds.push_str(&format!("  v{i}: i64 = {text}\n"));
                    format!("v{i}")
                }
                Written::Passed => format!("same({text})"),
                _ => text,
            });
        }
        (binds, args.join(", "))
    }
}

const SAME: &str = "fn same[T](x: T) -> T { x }\n\n";

/// A call of a function that gives back one of its arguments, whose
/// program is written with literals and with bindings of `i64`.
fn picking(case: &Call, literals: bool) -> String {
    let (binds, args) = case.arguments(literals);
    format!(
        "{SAME}fn pick{} -> {} {{ {} }}\n\nfn main() ! console, alloc {{\n{binds}  print(\"{{pick({args})}}\")\n}}\n",
        case.signature(),
        NAMES[case.chosen],
        PARAMS[case.chosen],
    )
}

fn a_literal_for_a_type_parameter_is_an_i64_as_a_binding_of_it_is() {
    // The program of the issue.
    let issue = "fn f[A, B](a: A, b: B) -> u8 { 1 }\n\nfn main() ! console, alloc { print(\"{f(1, 1)}\") }\n";
    let program = lower(issue.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
    assert_eq!(scalar::run(&program).stdout, b"1\n");

    generate::check(2110, 40, call, |case| {
        let expected = format!("{}\n", case.values[case.chosen]).into_bytes();
        for literals in [true, false] {
            let source = picking(case, literals);
            let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{source}"));
            assert_eq!(scalar::run(&program).stdout, expected, "{source}");
        }
    });
}

/// A function that needs `Show` of one of its parameters, and whether `i64`
/// has it.
#[derive(Debug)]
struct Needing {
    call: Call,
    i64_has_it: bool,
    /// Whether it is written as a bound or as a `requires`.
    bound: bool,
}

fn needing(g: &mut Gen) -> Needing {
    Needing {
        call: call(g),
        i64_has_it: g.one_in(2),
        bound: g.one_in(2),
    }
}

fn a_literal_for_a_type_parameter_that_needs_a_trait_compiles_when_i64_has_it() {
    generate::check(2111, 40, needing, |case| {
        let call = &case.call;
        let chosen = NAMES[call.chosen];
        let names: Vec<String> = (0..call.params)
            .map(|i| match (case.bound, i == call.chosen) {
                (true, true) => format!("{}: Show", NAMES[i]),
                _ => NAMES[i].to_string(),
            })
            .collect();
        let params: Vec<String> = (0..call.params)
            .map(|i| format!("{}: {}", PARAMS[i], NAMES[i]))
            .collect();
        let requires = if case.bound {
            String::new()
        } else {
            format!("\n  requires Show[{chosen}]")
        };
        let (binds, args) = call.arguments(true);
        // The unused type of a trait that `i64` does not have, so that the
        // literal is not the only thing that could be chosen.
        let mut source = format!(
            "{SAME}trait Show[T] {{\n  fn show(x: T) -> str\n}}\n\n\
             impl Show[u8] {{\n  fn show(x: u8) -> str {{ \"u8\" }}\n}}\n\n"
        );
        if case.i64_has_it {
            source.push_str("impl Show[i64] {\n  fn show(x: i64) -> str { \"i64\" }\n}\n\n");
        }
        source.push_str(&format!(
            "fn named[{}]({}) -> str{requires}\n{{\n  show({})\n}}\n\n",
            names.join(", "),
            params.join(", "),
            PARAMS[call.chosen],
        ));
        source.push_str("fn main() ! console, alloc {\n");
        source.push_str(&binds);
        let line = source.lines().count() + 1;
        source.push_str(&format!("  print(named({args}))\n}}\n"));
        match (lower(source.as_bytes()), case.i64_has_it) {
            (Ok(program), true) => {
                assert_eq!(scalar::run(&program).stdout, b"i64\n", "{source}")
            }
            (Err(e), true) => panic!("{e:?}\n{source}"),
            (Ok(_), false) => panic!("builds\n{source}"),
            (Err(found), false) => {
                assert_eq!(found.len(), 1, "{found:?}\n{source}");
                assert_eq!(found[0].pos, Pos { line, col: 9 }, "{}", found[0]);
                for name in ["Show", "`i64`"] {
                    assert!(found[0].message.contains(name), "{}", found[0]);
                }
            }
        }
    });
}

/// Two numbers given to a function of a trait that has an `out` type.
#[derive(Debug)]
struct Sum {
    left: i128,
    right: i128,
    i64_has_it: bool,
}

fn sum(g: &mut Gen) -> Sum {
    Sum {
        left: *g.pick(&VALUES),
        right: *g.pick(&VALUES),
        i64_has_it: g.one_in(2),
    }
}

fn two_literals_for_the_functions_of_a_trait_with_an_out_type_are_i64() {
    generate::check(2112, 40, sum, |case| {
        let mut source = String::from(
            "trait Plus[A, B, out C] {\n  fn plus(a: A, b: B) -> C\n}\n\n\
             impl Plus[u8, u8, u8] {\n  fn plus(a: u8, b: u8) -> u8 { wrapping_add(a, b) }\n}\n\n",
        );
        if case.i64_has_it {
            source.push_str(
                "impl Plus[i64, i64, i64] {\n  \
                 fn plus(a: i64, b: i64) -> i64 { wrapping_add(a, b) }\n}\n\n",
            );
        }
        source.push_str("fn main() ! console, alloc {\n");
        let line = source.lines().count() + 1;
        source.push_str(&format!(
            "  print(\"{{plus({}, {})}}\")\n}}\n",
            case.left, case.right
        ));
        match (lower(source.as_bytes()), case.i64_has_it) {
            (Ok(program), true) => {
                let sum = (case.left as i64).wrapping_add(case.right as i64);
                assert_eq!(scalar::run(&program).stdout, format!("{sum}\n").into_bytes());
            }
            (Err(e), true) => panic!("{e:?}\n{source}"),
            (Ok(_), false) => panic!("builds\n{source}"),
            (Err(found), false) => {
                assert!(!found.is_empty());
                assert_eq!(found[0].pos.line, line, "{}", found[0]);
                assert!(found[0].message.contains("Plus"), "{}", found[0]);
            }
        }
    });
}

/// What is written as the argument of a parameter, which may be no number.
const ARGUMENTS: [&str; 5] = ["7", "-3", "\"s\"", "true", "same(2)"];

/// A call of a function of three type parameters and a `str`, with an
/// argument of any of these kinds for each parameter.
fn anything(g: &mut Gen) -> String {
    let args: Vec<&str> = (0..4).map(|_| *g.pick(&ARGUMENTS)).collect();
    let nested = g.one_in(3);
    let inner = format!("pair({}, {})", args[0], args[1]);
    let call = if nested {
        format!("pair({inner}, {})", args[2])
    } else {
        format!("f({}, {}, {}, {})", args[0], args[1], args[2], args[3])
    };
    let source = format!(
        "{SAME}fn pair[A, B](a: A, b: B) -> u8 {{ 1 }}\n\n\
         fn f[A, B, C](a: A, b: B, c: C, s: str) -> u8 {{ 1 }}\n\n\
         fn main() ! console, alloc {{ print(\"{{{call}}}\") }}\n"
    );
    source
}

fn a_literal_among_any_arguments_never_panics_the_compiler() {
    generate::check(2113, 60, anything, |source| {
        // The compiler gives an error or a program, and does not stop.
        let _ = lower(source.as_bytes());
    });
}
