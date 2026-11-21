//! `f32` and `f64` as the language says: a float written out, `+`, `-`, `*`,
//! and `/`, a `-` before a float, the comparisons, `float(n)`, and floats in a
//! struct, a list, an argument, and a result. Every answer is what Rust gives
//! for the same operation on the same bits, and every executable of a chip
//! that builds floats gives what the evaluator gives.
//!
//! The values are drawn at the edges of each format and at random, and they
//! come out of a list, so that no target can know them. A float that is not
//! a number or is infinite is written as the `0.0 / 0.0` or `1.0 / 0.0` that
//! gives it.

use smith::scalar;
use smith::target::Target;
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::numbers::with_header;
use crate::targets::{runnable, runs_as};

pub const ONCE: &[Test] = &[
    (
        "the_evaluator_gives_what_rust_gives_for_every_operator_of_f32",
        || answers_are_what_rust_says::<f32>(0),
    ),
    (
        "the_evaluator_gives_what_rust_gives_for_every_operator_of_f64",
        || answers_are_what_rust_says::<f64>(1),
    ),
    (
        "a_whole_number_and_a_float_in_one_operator_are_refused",
        a_whole_number_and_a_float_in_one_operator_are_refused,
    ),
    (
        "a_float_too_large_for_its_type_is_refused",
        a_float_too_large_for_its_type_is_refused,
    ),
    (
        "a_float_literal_takes_the_type_its_use_decides_and_is_an_f64_when_nothing_does",
        a_float_literal_takes_the_type_its_use_decides_and_is_an_f64_when_nothing_does,
    ),
];

pub const MACHINE: &[Test] = &[
    (
        "every_executable_gives_what_rust_gives_for_every_operator_of_f32",
        || answers_on_every_executable::<f32>(0),
    ),
    (
        "every_executable_gives_what_rust_gives_for_every_operator_of_f64",
        || answers_on_every_executable::<f64>(1),
    ),
];

/// What a float type must give for the program to be written and its answers
/// worked out: Rust's own operations on it.
trait Float: Copy + PartialOrd + 'static {
    const NAME: &'static str;
    /// The bit that is the sign.
    const SIGN: u64;
    /// The values at the edges of the format.
    fn edges() -> Vec<Self>;
    /// A float of the bits, which are those of the type's width.
    fn from_bits(bits: u64) -> Self;
    fn is_nan(self) -> bool;
    fn is_infinite(self) -> bool;
    fn negative(self) -> bool;
    fn text(self) -> String;
    fn add(self, other: Self) -> Self;
    fn sub(self, other: Self) -> Self;
    fn mul(self, other: Self) -> Self;
    fn div(self, other: Self) -> Self;
    fn of_int(n: i64) -> Self;
    fn bits(self) -> u64;
    /// The bits, with every float that is not a number one value.
    fn key(self) -> u64;
}

macro_rules! float {
    ($t:ident, $bits:ident, $name:literal, $edges:expr) => {
        impl Float for $t {
            const NAME: &'static str = $name;
            const SIGN: u64 = 1 << ($bits::BITS - 1);
            fn bits(self) -> u64 {
                u64::from(self.to_bits())
            }
            fn edges() -> Vec<$t> {
                $edges
            }
            fn from_bits(bits: u64) -> $t {
                $t::from_bits(bits as $bits)
            }
            fn is_nan(self) -> bool {
                $t::is_nan(self)
            }
            fn is_infinite(self) -> bool {
                $t::is_infinite(self)
            }
            fn negative(self) -> bool {
                self.is_sign_negative()
            }
            fn text(self) -> String {
                format!("{self:e}")
            }
            fn add(self, other: $t) -> $t {
                self + other
            }
            fn sub(self, other: $t) -> $t {
                self - other
            }
            fn mul(self, other: $t) -> $t {
                self * other
            }
            fn div(self, other: $t) -> $t {
                self / other
            }
            fn of_int(n: i64) -> $t {
                n as $t
            }
            fn key(self) -> u64 {
                if $t::is_nan(self) {
                    u64::MAX
                } else {
                    u64::from(self.to_bits())
                }
            }
        }
    };
}

float!(
    f32,
    u32,
    "f32",
    vec![
        0.0,
        -0.0,
        1.0,
        -1.0,
        0.5,
        0.1,
        3.0,
        f32::MAX,
        f32::MIN_POSITIVE,
        f32::from_bits(1),
        16_777_216.0,
        f32::NAN,
        f32::INFINITY,
        f32::NEG_INFINITY,
    ]
);
float!(
    f64,
    u64,
    "f64",
    vec![
        0.0,
        -0.0,
        1.0,
        -1.0,
        0.5,
        0.1,
        3.0,
        f64::MAX,
        f64::MIN_POSITIVE,
        f64::from_bits(1),
        9_007_199_254_740_992.0,
        f64::NAN,
        f64::INFINITY,
        f64::NEG_INFINITY,
    ]
);

/// How a value is written in a program: as the shortest text that reads back
/// as exactly it, or as the expression that gives it.
fn written<T: Float>(x: T) -> String {
    if x.is_nan() {
        "(0.0 / 0.0)".to_string()
    } else if x.is_infinite() && x.negative() {
        "(-1.0 / 0.0)".to_string()
    } else if x.is_infinite() {
        "(1.0 / 0.0)".to_string()
    } else {
        x.text()
    }
}

/// Whether `a` and `b` are the same value, a float that is not a number being
/// the same as itself and `-0.0` another value than `0.0`.
fn same<T: Float>(a: T, b: T) -> bool {
    a.key() == b.key()
}

/// Whether `a` is below `b`, with `-0.0` below `0.0` and a float that is not a
/// number above every other.
fn below<T: Float>(a: T, b: T) -> bool {
    match (a.is_nan(), b.is_nan()) {
        (true, _) => false,
        (false, true) => true,
        _ if a < b => true,
        _ => a == b && a.negative() && !b.negative(),
    }
}

/// The values the program works on: the edges and some that are drawn.
fn values<T: Float>(g: &mut Gen) -> Vec<T> {
    let edges = T::edges();
    // The three that are not numbers or are no finite number, and the two
    // zeros, which every operator must tell apart, are always there.
    let mut values: Vec<T> = edges[11..].to_vec();
    values.extend(&edges[..2]);
    for _ in 0..2 {
        values.push(*g.pick(&edges[2..11]));
    }
    for _ in 0..2 {
        let bits = (g.below(1 << 30) as u64) << 34 | (g.below(1 << 30) as u64) << 4 | g.below(16) as u64;
        let x = T::from_bits(bits);
        // A float of the bits, which is a number and not an infinity.
        if !x.is_nan() && !x.is_infinite() {
            values.push(x);
        }
    }
    values
}

/// The whole numbers `float(n)` is tried on.
fn ints(g: &mut Gen) -> Vec<i64> {
    let mut ns = vec![0, 1, -1, 16_777_217, i64::MAX, i64::MIN, 9_007_199_254_740_993];
    ns.push(g.below(1 << 30) as i64 * 977 - (1 << 39));
    ns
}

/// The source of a program, and what it must print.
fn program<T: Float>(g: &mut Gen) -> (String, String) {
    let t = T::NAME;
    let xs = values::<T>(g);
    let ns = ints(g);
    let mut source = format!(
        "struct Pair {{\n  a: {t}\n  b: {t}\n}}\n\n\
         fn add(p: Pair) -> {t} {{ p.a + p.b }}\n\n\
         fn flip(x: {t}) -> {t} {{ -x }}\n\n\
         fn line(a: {t}, b: {t}, s: {t}, d: {t}, p: {t}, q: {t}) ! console, alloc {{\n  \
         print(\"{{a + b == s}} {{a - b == d}} {{a * b == p}} {{a / b == q}} {{a < b}} {{a <= b}} {{a > b}} {{a >= b}} {{a == b}} {{a != b}}\")\n}}\n\n\
         fn whole(n: Int, a: {t}) ! console, alloc {{\n  print(\"{{float(n) == a}}\")\n}}\n\n\
         fn main() ! console, alloc {{\n  xs: List[{t}] = [{}]\n  ns: List[Int] = [{}]\n",
        xs.iter().map(|x| written(*x)).collect::<Vec<_>>().join(", "),
        ns.iter().map(|n| n.to_string()).collect::<Vec<_>>().join(", ")
    );
    let mut said = String::new();
    for (i, &a) in xs.iter().enumerate() {
        for (j, &b) in xs.iter().enumerate() {
            let results = [a.add(b), a.sub(b), a.mul(b), a.div(b)];
            let args: Vec<String> = results.iter().map(|r| written(*r)).collect();
            source += &format!("  line(xs[{i}], xs[{j}], {})\n", args.join(", "));
            let held = [
                true,
                true,
                true,
                true,
                below(a, b),
                !below(b, a),
                below(b, a),
                !below(a, b),
                same(a, b),
                !same(a, b),
            ];
            let words: Vec<String> = held.iter().map(|h| h.to_string()).collect();
            said += &format!("{}\n", words.join(" "));
        }
    }
    for (i, &n) in ns.iter().enumerate() {
        source += &format!("  whole(ns[{i}], {})\n", written(T::of_int(n)));
        said += "true\n";
    }
    // A float in a struct, an argument, and a result, and the sign of one flipped.
    let (a, b) = (xs[0], xs[1]);
    source += &format!(
        "  p = Pair(a: xs[0], b: xs[1])\n  print(\"{{add(p) == {}}} {{flip(xs[0]) == {}}}\")\n",
        written(a.add(b)),
        written(neg(a)),
    );
    said += "true true\n";
    source += "}\n";
    (with_header(source), said)
}

/// `-x`: the float with its sign bit flipped.
fn neg<T: Float>(x: T) -> T {
    T::from_bits(x.bits() ^ T::SIGN)
}

fn answers_are_what_rust_says<T: Float>(seed: u64) {
    let (source, said) = generate::sample(seed, 0, |g| program::<T>(g));
    let lowered = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let outcome = scalar::run(&scalar::passes(&lowered));
    assert_eq!(String::from_utf8_lossy(&outcome.stdout), said, "for {source}");
    assert_eq!(outcome.status, 0);
}

fn answers_on_every_executable<T: Float>(seed: u64) {
    let (source, said) = generate::sample(seed, 0, |g| program::<T>(g));
    let outcome = scalar::Outcome {
        stdout: said.into_bytes(),
        status: 0,
    };
    for target in runnable()
        .into_iter()
        .filter(|t: &Target| t.builds_floats())
    {
        runs_as(source.as_bytes(), target, &outcome);
    }
}

/// A program of `items` whose `main` prints the `bool` that `shown` says, so
/// that nothing in it is dead code.
fn reaching(items: &str, shown: &str) -> String {
    with_header(format!(
        "{items}\nfn main() ! console, alloc {{\n  print(\"{{{shown}}}\")\n}}\n"
    ))
}

fn a_whole_number_and_a_float_in_one_operator_are_refused() {
    for operator in ["+", "-", "*", "/"] {
        let source = reaching(
            &format!("fn f(n: Int, x: f64) -> f64 {{ n {operator} x }}"),
            "f(1, 2.0) == 3.0",
        );
        let errors = check(source.as_bytes()).expect_err("a whole number and a float");
        assert!(
            errors.iter().any(|e| e.message.contains("float")),
            "{operator}: {errors:?}"
        );
    }
    for operator in ["<", "<=", ">", ">=", "==", "!="] {
        let source = reaching(
            &format!("fn f(n: Int, x: f64) -> bool {{ n {operator} x }}"),
            "f(1, 2.0)",
        );
        let errors = check(source.as_bytes()).expect_err("a whole number and a float");
        assert!(
            errors.iter().any(|e| e.message.contains("f64")),
            "{operator}: {errors:?}"
        );
    }
    let source = reaching("fn half(n: Int) -> f64 { n / 2.0 }", "half(5) == 2.5");
    assert!(check(source.as_bytes()).is_err(), "{source}");
    let source = reaching("fn half(n: Int) -> f64 { float(n) / 2.0 }", "half(5) == 2.5");
    assert!(check(source.as_bytes()).is_ok(), "{source}");
}

fn a_float_too_large_for_its_type_is_refused() {
    let source = reaching("fn big() -> f32 { 1e39 }", "big() == 1.0");
    assert!(check(source.as_bytes()).is_err(), "{source}");
    let source = reaching("fn big() -> f64 { 1e39 }", "big() == 1.0");
    assert!(check(source.as_bytes()).is_ok(), "{source}");
    let source = reaching("fn big() -> f64 { 1e999 }", "big() == 1.0");
    assert!(check(source.as_bytes()).is_err(), "{source}");
}

fn a_float_literal_takes_the_type_its_use_decides_and_is_an_f64_when_nothing_does() {
    // `0.1` is the `f32` nearest a tenth where an `f32` is wanted, and the
    // `f64` nearest it where nothing decides, so the two are not equal as
    // floats once the first is widened: each is compared as its own type.
    let source = with_header(
        "fn tenth() -> f32 { 0.1 }\n\nfn main() ! console, alloc {\n  \
         x = 0.1\n  y: f32 = 0.1\n  print(\"{x == 0.1} {y == tenth()}\")\n}\n"
            .to_string(),
    );
    let lowered = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let outcome = scalar::run(&scalar::passes(&lowered));
    assert_eq!(String::from_utf8_lossy(&outcome.stdout), "true true\n");
}
