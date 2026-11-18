//! A whole number written as the argument of a parameter whose type is a type
//! parameter, which nothing else decides, is an `i64`.

use smith::diag::Pos;
use smith::lower;
use smith::scalar;

use crate::Test;
use crate::generate::{self, Gen};

/// The whole number types, with their names, smallest and largest values.
const INTS: [(&str, i128, i128); 8] = [
    ("i8", i8::MIN as i128, i8::MAX as i128),
    ("i16", i16::MIN as i128, i16::MAX as i128),
    ("i32", i32::MIN as i128, i32::MAX as i128),
    ("i64", i64::MIN as i128, i64::MAX as i128),
    ("u8", 0, u8::MAX as i128),
    ("u16", 0, u16::MAX as i128),
    ("u32", 0, u32::MAX as i128),
    ("u64", 0, u64::MAX as i128),
];

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
    (
        "a_literal_for_a_type_parameter_takes_the_type_the_place_of_the_result_needs",
        a_literal_for_a_type_parameter_takes_the_type_the_place_of_the_result_needs,
    ),
    (
        "a_literal_the_place_of_the_result_cannot_hold_is_refused_at_the_literal",
        a_literal_the_place_of_the_result_cannot_hold_is_refused_at_the_literal,
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

/// Where a call's result is put, with a type that may be none it gives.
const PLACES: [&str; 8] = [
    "{}",
    "x: u8 = {}",
    "x: str = {}",
    "takes({})",
    "x == {}",
    "{} == x",
    "takes(same({}))",
    "(a, b): (u8, bool) = {}",
];

/// A call of a function of type parameters and a `str`, with an argument of
/// any of these kinds for each parameter, put where a type is needed.
fn anything(g: &mut Gen) -> String {
    let args: Vec<&str> = (0..4).map(|_| *g.pick(&ARGUMENTS)).collect();
    let nested = g.one_in(3);
    let call = match g.below(4) {
        _ if nested => format!("pair(pair({}, {}), {})", args[0], args[1], args[2]),
        0 => format!("f({}, {}, {}, {})", args[0], args[1], args[2], args[3]),
        1 => format!("both({}, {})", args[0], args[1]),
        2 => format!("same({})", args[0]),
        _ => format!("pair({}, {})", args[0], args[1]),
    };
    let place = PLACES[g.below(PLACES.len())].replace("{}", &call);
    format!(
        "{SAME}fn pair[A, B](a: A, b: B) -> u8 {{ 1 }}\n\n\
         fn both[A, B](a: A, b: B) -> (A, B) {{ (a, b) }}\n\n\
         fn f[A, B, C](a: A, b: B, c: C, s: str) -> A {{ a }}\n\n\
         fn takes(n: u8) -> u8 {{ n }}\n\n\
         fn main() ! console, alloc {{\n  x: u8 = 1\n  {place}\n}}\n"
    )
}

fn a_literal_among_any_arguments_never_panics_the_compiler() {
    generate::check(2113, 60, anything, |source| {
        // The compiler gives an error or a program, and does not stop.
        let _ = lower(source.as_bytes());
    });
}

/// Where the result of a call is needed to be of a whole number type.
#[derive(Clone, Copy, Debug)]
enum Place {
    /// A binding that states its type.
    Binding,
    /// An argument of a function that takes the type.
    Argument,
    /// The side of an operation whose other side has the type.
    Operand,
    /// A field of that type.
    Field,
    /// The value a function that gives the type returns.
    Returned,
}

/// A generic function whose result names a type parameter, and which
/// parameter that is.
#[derive(Clone, Copy, Debug)]
enum Shape {
    Same,
    First,
    Second,
}

const SHAPES: &str = "fn same[A](a: A) -> A { a }\n\n\
     fn first[A, B](a: A, b: B) -> A { a }\n\n\
     fn second[A, B](a: A, b: B) -> B { b }\n\n";

#[derive(Debug)]
struct Placed {
    int: usize,
    value: i128,
    shape: Shape,
    place: Place,
    /// How many calls the number is nested in.
    depth: usize,
    /// For an operand: the operator, and whether the call is on the left.
    op: usize,
    left: bool,
}

/// The operators and functions that take two values of one type.
const OPERATORS: [&str; 12] = [
    "==",
    "!=",
    "<",
    "<=",
    ">",
    ">=",
    "+",
    "-",
    "*",
    "wrapping_add",
    "wrapping_sub",
    "wrapping_mul",
];

/// The operators whose result must be proven to fit, which may refuse a
/// program that is no mistake of the type.
fn proven(op: usize) -> bool {
    (6..9).contains(&op)
}

fn placed(g: &mut Gen) -> Placed {
    let int = g.below(INTS.len());
    let (_, low, high) = INTS[int];
    let value = match g.below(5) {
        0 => low,
        1 => high,
        2 => 0,
        3 => 1.clamp(low, high),
        _ => low + (g.below(1 << 20) as i128 * (high - low) / (1 << 20)),
    };
    Placed {
        int,
        value,
        depth: 1 + g.below(3),
        op: g.below(OPERATORS.len()),
        left: g.one_in(2),
        shape: *g.pick(&[Shape::Same, Shape::First, Shape::Second]),
        place: *g.pick(&[
            Place::Binding,
            Place::Argument,
            Place::Operand,
            Place::Field,
            Place::Returned,
        ]),
    }
}

impl Placed {
    /// The program, with the number written as the argument, or bound to a
    /// name of the type first, and the text the number is written as.
    fn program(&self, literal: bool, number: &str) -> String {
        let ty = INTS[self.int].0;
        let (arg, binds) = match literal {
            true => (number.to_string(), String::new()),
            false => ("v".to_string(), format!("  v: {ty} = {number}\n")),
        };
        let mut call = arg;
        for _ in 0..self.depth {
            call = match self.shape {
                Shape::Same => format!("same({call})"),
                Shape::First => format!("first({call}, 2)"),
                Shape::Second => format!("second(2, {call})"),
            };
        }
        let (items, main) = match self.place {
            Place::Binding => (String::new(), format!("{binds}  r: {ty} = {call}\n  print(\"{{r}}\")\n")),
            Place::Argument => (
                format!("fn takes(n: {ty}) -> {ty} {{ n }}\n\n"),
                format!("{binds}  print(\"{{takes({call})}}\")\n"),
            ),
            Place::Operand => {
                let op = OPERATORS[self.op];
                let (l, r) = match self.left {
                    true => (call.as_str(), "k"),
                    false => ("k", call.as_str()),
                };
                let used = match op.starts_with("wrapping") {
                    true => format!("{op}({l}, {r})"),
                    false => format!("{l} {op} {r}"),
                };
                (String::new(), format!("{binds}  k: {ty} = 1\n  print(\"{{{used}}}\")\n"))
            }
            Place::Field => (
                format!("struct S {{ f: {ty} }}\n\n"),
                format!("{binds}  s = S(f: {call})\n  print(\"{{s.f}}\")\n"),
            ),
            Place::Returned => {
                let holder = format!("fn back() -> {ty} {{\n{binds}  {call}\n}}\n\n");
                (holder, "  print(\"{back()}\")\n".to_string())
            }
        };
        format!("{SHAPES}{items}fn main() ! console, alloc {{\n{main}}}\n")
    }
}

fn a_literal_for_a_type_parameter_takes_the_type_the_place_of_the_result_needs() {
    // The program of the issue.
    let issue = "fn id[A](a: A) -> A { a }\n\nfn takes(n: u8) -> u8 { n }\n\n\
        fn pair[A, B](a: A, b: B) -> (A, B) { (a, b) }\n\n\
        fn main() ! console, alloc {\n  y: u8 = id(7)\n  (r, s): (u8, i32) = pair(3, 4)\n  \
        k: u8 = 200\n  print(\"{y} {r} {s} {takes(id(7))} {wrapping_add(k, id(5))}\")\n}\n";
    let program = lower(issue.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
    assert_eq!(scalar::run(&program).stdout, b"7 3 4 7 205\n");

    generate::check(2114, 60, placed, |case| {
        let number = case.value.to_string();
        let mut printed = Vec::new();
        for literal in [true, false] {
            let source = case.program(literal, &number);
            printed.push(match lower(source.as_bytes()) {
                Ok(program) => Ok(scalar::run(&program).stdout),
                Err(found) => Err(found),
            });
        }
        match (&printed[0], &printed[1]) {
            (Ok(a), Ok(b)) => assert_eq!(a, b, "{}", case.program(true, &number)),
            // The proof that a result fits is of the program as written, so
            // it may refuse both, and then only for that.
            (Err(a), Err(_)) if case.is_proven() => {
                let source = case.program(true, &number);
                assert!(a.iter().all(|e| e.message.contains("cannot prove")), "{a:?}\n{source}")
            }
            (a, b) => panic!("{a:?} {b:?}\n{}", case.program(true, &number)),
        }
    });
}

impl Placed {
    fn is_proven(&self) -> bool {
        matches!(self.place, Place::Operand) && proven(self.op)
    }
}

/// The place of `needle`, a number that is written once, in `source`.
fn place_of(source: &str, needle: &str) -> Pos {
    let at = source.find(needle).unwrap();
    let line = source[..at].matches('\n').count() + 1;
    let col = at - source[..at].rfind('\n').map_or(0, |n| n + 1) + 1;
    Pos { line, col }
}

fn a_literal_the_place_of_the_result_cannot_hold_is_refused_at_the_literal() {
    generate::check(2115, 40, placed, |case| {
        let (ty, low, high) = INTS[case.int];
        let number = match case.value % 2 {
            0 => (high + 1).to_string(),
            _ => (low - 1).to_string(),
        };
        let source = case.program(true, &number);
        let Err(found) = lower(source.as_bytes()) else {
            panic!("builds\n{source}");
        };
        let at = place_of(&source, &number);
        // A sum that cannot be proven to fit may be refused as well.
        let here: Vec<_> = found.iter().filter(|e| e.pos == at).collect();
        assert_eq!(here.len(), 1, "{found:?}\n{source}");
        assert!(case.is_proven() || found.len() == 1, "{found:?}\n{source}");
        for name in [ty, &low.to_string(), &high.to_string()] {
            assert!(here[0].message.contains(name), "{}", here[0]);
        }
    });
}
