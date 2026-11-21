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
        "the_text_of_an_f32_is_its_shortest_digits_laid_out_as_the_page_says",
        || texts_are_what_rust_says::<f32>(2),
    ),
    (
        "the_text_of_an_f64_is_its_shortest_digits_laid_out_as_the_page_says",
        || texts_are_what_rust_says::<f64>(3),
    ),
    (
        "a_tie_between_two_shortest_texts_goes_to_the_even_digit",
        a_tie_between_two_shortest_texts_goes_to_the_even_digit,
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
    (
        "every_executable_prints_2_5_for_half_of_5",
        every_executable_prints_2_5_for_half_of_5,
    ),
    (
        "every_executable_writes_the_shortest_text_of_an_f32",
        || texts_on_every_executable::<f32>(2),
    ),
    (
        "every_executable_writes_the_shortest_text_of_an_f64",
        || texts_on_every_executable::<f64>(3),
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
    /// The float a text reads as, if it is a number this type holds.
    fn parsed(text: &str) -> Option<Self>;
    /// The bits, with every float that is not a number one value.
    fn key(self) -> u64;
}

macro_rules! float {
    ($t:ident, $bits:ident, $name:literal, $edges:expr) => {
        impl Float for $t {
            const NAME: &'static str = $name;
            const SIGN: u64 = 1 << ($bits::BITS - 1);
            fn parsed(text: &str) -> Option<$t> {
                text.parse::<$t>().ok().filter(|x| x.is_finite())
            }
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

/// The text of a float that the page says: `nan`, `inf`, `-inf`, `0.0` and
/// `-0.0`, and for any other the shortest digits, which Rust's `{:e}` gives,
/// with a point and no exponent from `10^-5` to `10^16`, and otherwise as
/// `d.ddde-x`, with no point for one digit.
fn spelled<T: Float>(x: T) -> String {
    if x.is_nan() {
        return "nan".to_string();
    }
    let sign = if x.negative() { "-" } else { "" };
    if x.is_infinite() {
        return format!("{sign}inf");
    }
    let shown = x.text();
    let shown = shown.trim_start_matches('-');
    if shown == "0e0" {
        return format!("{sign}0.0");
    }
    let (mantissa, exponent) = shown.split_once('e').expect("an exponent");
    let exponent: i32 = exponent.parse().expect("a number");
    let digits: String = mantissa.chars().filter(|c| *c != '.').collect();
    let n = digits.len() as i32;
    let body = if (-5..=16).contains(&exponent) {
        if exponent >= 0 {
            let int = exponent + 1;
            if n <= int {
                format!("{digits}{}.0", "0".repeat((int - n) as usize))
            } else {
                format!("{}.{}", &digits[..int as usize], &digits[int as usize..])
            }
        } else {
            format!("0.{}{digits}", "0".repeat((-exponent - 1) as usize))
        }
    } else if n == 1 {
        format!("{digits}e{exponent}")
    } else {
        format!("{}.{}e{exponent}", &digits[..1], &digits[1..])
    };
    format!("{sign}{body}")
}

/// The floats whose text is tried: the edges of the format, powers of 2 and of
/// 10, a number of each length, and some drawn at random.
fn texts<T: Float>(g: &mut Gen) -> Vec<T> {
    let mut values: Vec<T> = T::edges();
    for k in 0..4 {
        values.push(T::from_bits(1u64 << (T::SIGN.trailing_zeros() - 1 - k)));
    }
    for text in [
        "1e0", "1e1", "1e5", "1e15", "1e16", "1e17", "1e21", "1e22", "1e23", "1e-4", "1e-5",
        "1e-6", "1.5e-7", "5e-324", "2.2250738585072014e-308", "123456789e0", "0.3", "1e10",
        "9.999999e15", "4.35e0", "8.41e21",
    ] {
        // Only the numbers the type can hold.
        if let Some(x) = T::parsed(text) {
            values.push(x);
            values.push(neg(x));
        }
    }
    // The powers of 2, and the numbers just above and below them, where the
    // interval of the numbers that round to a float is not symmetric.
    let (mantissa, top) = if T::NAME == "f32" { (23, 254) } else { (52, 2046) };
    for e in (1..top).step_by(if T::NAME == "f32" { 7 } else { 29 }) {
        let power = (e as u64) << mantissa;
        values.push(T::from_bits(power));
        values.push(T::from_bits(power + 1));
        values.push(T::from_bits(power - 1));
    }
    for _ in 0..40 {
        let bits = (g.below(1 << 30) as u64) << 34 | (g.below(1 << 30) as u64) << 4 | g.below(16) as u64;
        let x = T::from_bits(bits ^ ((g.below(1 << 20) as u64) << 20));
        if !x.is_nan() && !x.is_infinite() {
            values.push(x);
        }
    }
    values
}

/// The program that prints each float on a line, and what it must print.
fn text_program<T: Float>(g: &mut Gen) -> (String, String) {
    let values = texts::<T>(g);
    let t = T::NAME;
    let mut source = format!(
        "fn main() ! console, alloc {{\n  xs: List[{t}] = [{}]\n",
        values.iter().map(|x| written(*x)).collect::<Vec<_>>().join(", ")
    );
    let mut said = String::new();
    for (i, x) in values.iter().enumerate() {
        source += &format!("  print(\"{{xs[{i}]}}\")\n");
        said += &format!("{}\n", spelled(*x));
        // The text of a number is a float that is written out, which reads back
        // as the float.
        if !x.is_nan() && !x.is_infinite() {
            source += &format!("  print(\"{{xs[{i}] == {}}}\")\n", spelled(*x));
            said += "true\n";
        }
    }
    source += "}\n";
    (with_header(source), said)
}

/// Whether the text is what Rust writes, or the other of two shortest texts
/// that are equally near the float, which Rust rounds up where Ryu goes to
/// the even digit: the same number of digits, each a number that reads back as
/// the float, and the last digit apart by 1.
fn same_text<T: Float>(ours: &str, theirs: &str) -> bool {
    if ours == theirs {
        return true;
    }
    let digits = |t: &str| -> Vec<char> {
        let mantissa = t.split('e').next().unwrap_or(t);
        mantissa.chars().filter(|c| c.is_ascii_digit()).collect()
    };
    let (a, b) = (digits(ours), digits(theirs));
    let same_shape = ours.len() == theirs.len()
        && ours.chars().zip(theirs.chars()).filter(|(x, y)| x != y).count() == 1;
    let (Some(x), Some(y)) = (T::parsed(ours), T::parsed(theirs)) else {
        return false;
    };
    // The two read back as one float, so they are two shortest texts of it.
    same_shape && a.len() == b.len() && x.bits() == y.bits()
}

fn texts_are_what_rust_says<T: Float>(seed: u64) {
    let (source, said) = generate::sample(seed, 0, |g| text_program::<T>(g));
    let lowered = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let outcome = scalar::run(&scalar::passes(&lowered));
    let got = String::from_utf8_lossy(&outcome.stdout).to_string();
    let (got, want): (Vec<&str>, Vec<&str>) = (got.lines().collect(), said.lines().collect());
    assert_eq!(got.len(), want.len(), "for {source}");
    for (ours, theirs) in got.iter().zip(&want) {
        assert!(same_text::<T>(ours, theirs), "{ours} is not {theirs} for {source}");
    }
}

/// Every executable writes what the evaluator writes, which the other test
/// holds to what Rust writes.
fn texts_on_every_executable<T: Float>(seed: u64) {
    let (source, _) = generate::sample(seed, 0, |g| text_program::<T>(g));
    let lowered = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let outcome = scalar::run(&scalar::passes(&lowered));
    for target in runnable().into_iter().filter(|t: &Target| t.builds_floats()) {
        runs_as(source.as_bytes(), target, &outcome);
    }
}

/// `2097152.25` is as near `2097152.2` as `2097152.3`, and both read back as
/// it, so the digit that is even is the one written.
fn a_tie_between_two_shortest_texts_goes_to_the_even_digit() {
    let source = with_header(
        "fn main() ! console, alloc {\n  x: f32 = 2097152.25\n  y: f32 = 2097152.75\n  print(\"{x} {y}\")\n}\n"
            .to_string(),
    );
    let lowered = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let outcome = scalar::run(&scalar::passes(&lowered));
    assert_eq!(String::from_utf8_lossy(&outcome.stdout), "2097152.2 2097152.8\n");
}

/// The program of `number-types.md`: `half(5)` is `2.5`, on every target that
/// builds floats.
fn every_executable_prints_2_5_for_half_of_5() {
    let source = with_header(
        "fn half(n: Int) -> f64 { float(n) / 2.0 }\n\nfn main() ! console, alloc {\n  print(\"{half(5)}\")\n}\n"
            .to_string(),
    );
    let outcome = scalar::Outcome {
        stdout: b"2.5\n".to_vec(),
        status: 0,
    };
    for target in runnable().into_iter().filter(|t: &Target| t.builds_floats()) {
        runs_as(source.as_bytes(), target, &outcome);
    }
}

/// The sources of the programs of floats that the other tests of this file
/// run: every operator and comparison, and the text, of each size, so that a
/// target that is run by a host is held to the evaluator on them.
pub fn sources() -> Vec<String> {
    vec![
        generate::sample(0, 0, program::<f32>).0,
        generate::sample(1, 0, program::<f64>).0,
        generate::sample(2, 0, text_program::<f32>).0,
        generate::sample(3, 0, text_program::<f64>).0,
    ]
}
