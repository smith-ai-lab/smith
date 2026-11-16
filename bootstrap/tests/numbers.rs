//! The generator of programs that hold, print, compare, and do arithmetic on
//! whole numbers, which can also place one mistake in what it builds.
//!
//! A program binds names of whole number types to literals, each written in
//! a form the lexical rules allow, and its `main` prints lines whose strings
//! hold those names, their `repr`, comparisons of them, `wrapping_`,
//! `saturating_`, and `checked_` arithmetic on them, and tuples and a struct
//! that hold them. The generator draws a value from the least and greatest
//! of its type, and from just inside them, as often as from anywhere else.
//!
//! The generator works out what the program prints with the arithmetic of
//! Rust on 128 bits, which no type here overflows, so it is an oracle that
//! shares nothing with the compiler.

use crate::generate::Gen;
use crate::programs::{Expected, pos_after};

/// A whole number type.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Int {
    pub name: &'static str,
    pub bits: u32,
    pub signed: bool,
}

pub const INTS: &[Int] = &[
    Int::new("i32", 32, true),
    Int::new("u8", 8, false),
    Int::new("i8", 8, true),
    Int::new("u16", 16, false),
    Int::new("i16", 16, true),
    Int::new("u32", 32, false),
    Int::new("i64", 64, true),
    Int::new("u64", 64, false),
];

impl Int {
    const fn new(name: &'static str, bits: u32, signed: bool) -> Int {
        Int { name, bits, signed }
    }

    pub fn min(self) -> i128 {
        if self.signed {
            -(1i128 << (self.bits - 1))
        } else {
            0
        }
    }

    pub fn max(self) -> i128 {
        if self.signed {
            (1i128 << (self.bits - 1)) - 1
        } else {
            (1i128 << self.bits) - 1
        }
    }

    /// The value with the bits that do not fit left off.
    fn wrap(self, value: i128) -> i128 {
        let size = 1i128 << self.bits;
        (value - self.min()).rem_euclid(size) + self.min()
    }
}

/// A generated program: its source, what it prints, the values it binds,
/// and the mistake placed in it, if any.
pub struct NumberCase {
    pub source: String,
    pub stdout: Vec<u8>,
    /// The type and value of each name it binds.
    pub bound: Vec<(Int, i128)>,
    pub mistake: Option<Expected>,
}

impl std::fmt::Debug for NumberCase {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self.source)
    }
}

#[derive(Clone, Debug)]
struct Var {
    name: String,
    ty: Int,
    value: i128,
}

/// A value of the type: its least, its greatest, a few next to them, and
/// any other.
pub fn value(g: &mut Gen, ty: Int) -> i128 {
    match g.below(9) {
        0 => ty.min(),
        1 => ty.max(),
        2 => 0,
        3 => 1.clamp(ty.min(), ty.max()),
        4 => ty.min() + 1,
        5 => ty.max() - 1,
        6 => (-1i128).clamp(ty.min(), ty.max()),
        _ => {
            let high = g.below(1 << 32) as u64;
            let low = g.below(1 << 32) as u64;
            let bits = high << 32 | low;
            if ty.signed {
                ty.wrap(bits as i64 as i128)
            } else {
                (bits as i128) & ty.max()
            }
        }
    }
}

/// How a literal for the value is written: in decimal, or for a value that
/// is not negative in hexadecimal or binary too, with an `_` between two of
/// its digits sometimes.
fn spelled(g: &mut Gen, value: i128) -> String {
    let (sign, prefix, digits) = if value < 0 {
        ("-", "", (-value).to_string())
    } else {
        match g.below(4) {
            0 => ("", "0x", format!("{value:x}")),
            1 => ("", "0b", format!("{value:b}")),
            _ => ("", "", value.to_string()),
        }
    };
    let mut digits = digits;
    if digits.len() > 1 && g.one_in(3) {
        let at = 1 + g.below(digits.len() - 1);
        digits.insert(at, '_');
    }
    format!("{sign}{prefix}{digits}")
}

/// The pieces of one printed line: the text of the string as written, and
/// what it prints.
struct Line {
    written: String,
    printed: String,
}

impl Line {
    fn new() -> Line {
        Line {
            written: String::new(),
            printed: String::new(),
        }
    }

    fn text(&mut self, text: &str) {
        self.written.push_str(text);
        self.printed.push_str(text);
    }

    /// An expression in braces, and what it prints.
    fn braced(&mut self, expr: &str, printed: &str) {
        self.written.push_str(&format!("{{{expr}}}"));
        self.printed.push_str(printed);
    }
}

fn operand(g: &mut Gen, vars: &[Var], ty: Int) -> (String, i128) {
    if g.one_in(3) {
        let v = value(g, ty);
        (spelled(g, v), v)
    } else {
        let same: Vec<&Var> = vars.iter().filter(|v| v.ty == ty).collect();
        let other = *g.pick(&same);
        (other.name.clone(), other.value)
    }
}

pub fn compare(op: &str, a: i128, b: i128) -> bool {
    match op {
        "==" => a == b,
        "!=" => a != b,
        "<" => a < b,
        "<=" => a <= b,
        ">" => a > b,
        _ => a >= b,
    }
}

/// What `wrapping_`, `saturating_`, or `checked_` of the operation gives
/// for the values, as text.
pub fn arithmetic(kind: &str, op: &str, ty: Int, a: i128, b: i128) -> String {
    // A product of two values of a type of 64 bits may not fit even 128
    // bits, and then it fits no type, and the bits that stay are the same.
    let exact = match op {
        "add" => a.checked_add(b),
        "sub" => a.checked_sub(b),
        _ => a.checked_mul(b),
    };
    let kept = match op {
        "add" => a.wrapping_add(b),
        "sub" => a.wrapping_sub(b),
        _ => a.wrapping_mul(b),
    };
    let fits = exact.filter(|e| (ty.min()..=ty.max()).contains(e));
    match (kind, fits) {
        ("wrapping", _) => ty.wrap(kept).to_string(),
        ("saturating", Some(e)) => e.to_string(),
        ("saturating", None) => {
            let above = match op {
                "sub" => b < 0,
                "add" => b > 0,
                _ => (a < 0) == (b < 0),
            };
            let limit = if above { ty.max() } else { ty.min() };
            // A subtraction below the least value of an unsigned type.
            if op == "sub" && !ty.signed { 0 } else { limit }.to_string()
        }
        (_, Some(e)) => format!(".some({e})"),
        _ => ".none".to_string(),
    }
}

/// A line that prints something about the values the program binds.
fn line(g: &mut Gen, vars: &[Var]) -> Line {
    let mut line = Line::new();
    let first = g.pick(vars).clone();
    match g.below(7) {
        0 => {
            for (i, v) in std::iter::once(&first)
                .chain((0..g.below(3)).map(|_| g.pick(vars)).collect::<Vec<_>>())
                .enumerate()
            {
                if i > 0 {
                    line.text(" ");
                }
                line.braced(&v.name, &v.value.to_string());
            }
        }
        1 => {
            line.text("repr ");
            line.braced(&format!("repr({})", first.name), &first.value.to_string());
        }
        2 => {
            let op = *g.pick(&["==", "!=", "<", "<=", ">", ">="]);
            // A literal on the left takes its type from the name on the right.
            let (left, a, other, b) = if g.one_in(4) {
                let v = value(g, first.ty);
                (spelled(g, v), v, first.name.clone(), first.value)
            } else {
                let (other, b) = operand(g, vars, first.ty);
                (first.name.clone(), first.value, other, b)
            };
            line.braced(
                &format!("{left} {op} {other}"),
                &compare(op, a, b).to_string(),
            );
        }
        3 | 4 => {
            let kind = *g.pick(&["wrapping", "saturating", "checked"]);
            let op = *g.pick(&["add", "sub", "mul"]);
            let (other, b) = operand(g, vars, first.ty);
            line.braced(
                &format!("{kind}_{op}({}, {other})", first.name),
                &arithmetic(kind, op, first.ty, first.value, b),
            );
        }
        5 => {
            let second = g.pick(vars).clone();
            line.braced(
                &format!("({}, {})", first.name, second.name),
                &format!("({}, {})", first.value, second.value),
            );
        }
        _ => {
            let same: Vec<&Var> = vars.iter().filter(|v| v.ty == vars[0].ty).collect();
            let (x, y) = ((*g.pick(&same)).clone(), (*g.pick(&same)).clone());
            let (z, w) = ((*g.pick(&same)).clone(), (*g.pick(&same)).clone());
            let made = |a: &Var, b: &Var| format!("Pair(a: {}, b: {})", a.name, b.name);
            line.braced(
                &made(&x, &y),
                &format!("Pair(a: {}, b: {})", x.value, y.value),
            );
            line.text(" ");
            line.braced(
                &format!("{} == {}", made(&x, &y), made(&z, &w)),
                &((x.value == z.value) && (y.value == w.value)).to_string(),
            );
        }
    }
    line
}

/// The model of a program: the names it binds, in order, and its lines.
struct Model {
    vars: Vec<Var>,
    lines: Vec<Line>,
}

/// The names a program binds: two or three of each of one type to three.
fn variables(g: &mut Gen) -> Vec<Var> {
    let kinds = 1 + g.below(3);
    let mut tys: Vec<Int> = vec![*g.pick(INTS)];
    for _ in 1..kinds {
        tys.push(*g.pick(INTS));
    }
    let mut vars = Vec::new();
    for ty in tys {
        for _ in 0..2 + g.below(2) {
            let value = value(g, ty);
            vars.push(Var {
                name: format!("n{}", vars.len()),
                ty,
                value,
            });
        }
    }
    vars
}

fn model(g: &mut Gen) -> Model {
    let vars = variables(g);
    let count = 1 + g.below(8);
    let lines = (0..count).map(|_| line(g, &vars)).collect();
    Model { vars, lines }
}

/// The program, with a mistake before the line `at`, if there is one. The
/// mistake is written as the text before the place it is reported, and the
/// text of the place and after it, as `(before, after)`.
fn write(
    g: &mut Gen,
    m: &Model,
    mistake: Option<(usize, &str, &str, &str, Option<&str>)>,
) -> (String, Option<Expected>) {
    let mut text = String::from("struct Pair {\n");
    let first = m.vars[0].ty;
    text.push_str(&format!("  a: {0}\n  b: {0}\n}}\n\n", first.name));
    // A function for each type that takes one, so that a name bound without
    // its type is decided by being passed to it.
    let mut taken: Vec<Int> = Vec::new();
    let mut bindings = String::new();
    for v in &m.vars {
        let spelling = spelled(g, v.value);
        if g.one_in(2) {
            bindings.push_str(&format!("  {} = {spelling}\n", v.name));
            if v.ty.name != "i64" {
                bindings.push_str(&format!("  id_{}({})\n", v.ty.name, v.name));
                if !taken.contains(&v.ty) {
                    taken.push(v.ty);
                }
            }
        } else {
            bindings.push_str(&format!("  {}: {} = {spelling}\n", v.name, v.ty.name));
        }
    }
    if let Some((_, before, after, ..)) = mistake {
        for ty in INTS {
            let call = format!("id_{}(", ty.name);
            if (before.contains(&call) || after.contains(&call)) && !taken.contains(ty) {
                taken.push(*ty);
            }
        }
    }
    for ty in taken {
        text.push_str(&format!("fn id_{0}(n: {0}) -> {0} {{ n }}\n\n", ty.name));
    }
    text.push_str("fn main() ! console, alloc {\n");
    text.push_str(&bindings);
    let mut expected = None;
    for (i, l) in m.lines.iter().enumerate() {
        if let Some((at, before, after, mentions, fix)) = mistake
            && at == i
        {
            text.push_str(&format!("  {before}"));
            expected = Some(Expected {
                at: pos_after(&text),
                mentions: mentions.to_string(),
                fix: fix.map(str::to_string),
            });
            text.push_str(&format!("{after}\n"));
        }
        text.push_str(&format!("  print(\"{}\")\n", l.written));
    }
    if let Some((at, before, after, mentions, fix)) = mistake
        && at >= m.lines.len()
    {
        text.push_str(&format!("  {before}"));
        expected = Some(Expected {
            at: pos_after(&text),
            mentions: mentions.to_string(),
            fix: fix.map(str::to_string),
        });
        text.push_str(&format!("{after}\n"));
    }
    text.push_str("}\n");
    (text, expected)
}

/// A program that holds, prints, compares, and does arithmetic on whole
/// numbers.
pub fn program(g: &mut Gen) -> NumberCase {
    let m = model(g);
    let (source, _) = write(g, &m, None);
    let stdout = (m.lines.iter())
        .map(|l| format!("{}\n", l.printed))
        .collect::<String>()
        .into_bytes();
    NumberCase {
        source,
        stdout,
        bound: m.vars.iter().map(|v| (v.ty, v.value)).collect(),
        mistake: None,
    }
}

/// A program with one mistake: a number with nothing to give it a type, one
/// that does not fit its type, two numbers of different types compared or
/// passed to one function, a plain `+`, `-`, or `*`, or a number where text
/// is needed.
pub fn program_with_mistake(g: &mut Gen) -> NumberCase {
    let m = model(g);
    let at = g.below(m.lines.len() + 1);
    let ty = *g.pick(INTS);
    let other = *g.pick(
        INTS.iter()
            .filter(|t| **t != ty)
            .collect::<Vec<_>>()
            .as_slice(),
    );
    let (a, b) = (g.pick(&m.vars).clone(), g.pick(&m.vars).clone());
    let too_far = if g.one_in(2) {
        ty.max() + 1 + g.below(1000) as i128
    } else if ty.signed {
        ty.min() - 1 - g.below(1000) as i128
    } else {
        -1 - g.below(1000) as i128
    };
    let sum = *g.pick(&["+", "-", "*"]);
    let kind = g.below(7);
    let literal = value(g, ty);
    let literal = spelled(g, literal);
    let (before, after, mentions): (String, String, String) = match kind {
        0 => (
            format!("x = 5\n  id_{}(x)\n  id_{}(", ty.name, other.name),
            "x)".into(),
            format!("where a `{}` is needed", other.name),
        ),
        1 => (
            format!("small: {} = ", ty.name),
            too_far.to_string(),
            format!("`{}`, which holds {} to {}", ty.name, ty.min(), ty.max()),
        ),
        2 => (
            format!(
                "one: {} = 1\n  two: {} = 2\n  print(\"{{one == ",
                ty.name, other.name
            ),
            "two}\")".into(),
            format!("where a `{}` is needed", ty.name),
        ),
        3 => (
            format!(
                "one: {} = 1\n  two: {} = 2\n  print(\"{{wrapping_add(one, ",
                ty.name, other.name
            ),
            "two)}\")".into(),
            format!("where a `{}` is needed", ty.name),
        ),
        4 => (
            format!("print(\"{{{} ", a.name),
            format!("{sum} {}}}\")", b.name),
            "not supported yet".into(),
        ),
        5 => (
            "print(".into(),
            format!("{literal})"),
            "where text is needed".into(),
        ),
        _ => (
            "x = ".into(),
            format!("{too_far}\n  id_{}(x)", ty.name),
            format!("does not fit `{}`", ty.name),
        ),
    };
    let fix: Option<&str> = None;
    let (source, mistake) = write(g, &m, Some((at, &before, &after, &mentions, fix)));
    NumberCase {
        source,
        stdout: Vec::new(),
        bound: Vec::new(),
        mistake,
    }
}

/// A line that decides between two fixed texts by numbers, and the text it
/// prints.
fn quiet_line(g: &mut Gen, vars: &[Var]) -> (String, &'static str) {
    let first = g.pick(vars).clone();
    let (yes, no) = (r#"print("yes")"#, r#"print("no")"#);
    let choose = |test: &str, truth: bool| {
        let source = format!("if {test} {{ {yes} }} else {{ {no} }}");
        (source, if truth { "yes" } else { "no" })
    };
    match g.below(5) {
        0 | 1 => {
            let op = *g.pick(&["==", "!=", "<", "<=", ">", ">="]);
            let (other, b) = operand(g, vars, first.ty);
            let test = format!("{} {op} {other}", first.name);
            choose(&test, compare(op, first.value, b))
        }
        2 => {
            let kind = *g.pick(&["wrapping", "saturating"]);
            let op = *g.pick(&["add", "sub", "mul"]);
            let (other, b) = operand(g, vars, first.ty);
            let (expected, c) = operand(g, vars, first.ty);
            let test = format!("{kind}_{op}({}, {other}) == {expected}", first.name);
            let result = arithmetic(kind, op, first.ty, first.value, b);
            choose(&test, result == c.to_string())
        }
        3 => {
            let op = *g.pick(&["add", "sub", "mul"]);
            let (other, b) = operand(g, vars, first.ty);
            let (expected, c) = operand(g, vars, first.ty);
            let source = format!(
                "match checked_{op}({}, {other}) {{\n    .some(x) {{ if x == {expected} {{ {yes} }} else {{ {no} }} }}\n    .none {{ print(\"none\") }}\n  }}",
                first.name
            );
            let printed = match arithmetic("checked", op, first.ty, first.value, b) {
                r if r == ".none" => "none",
                r if r == format!(".some({c})") => "yes",
                _ => "no",
            };
            (source, printed)
        }
        _ => {
            let same: Vec<&Var> = vars.iter().filter(|v| v.ty == vars[0].ty).collect();
            let (x, y) = ((*g.pick(&same)).clone(), (*g.pick(&same)).clone());
            let (z, w) = ((*g.pick(&same)).clone(), (*g.pick(&same)).clone());
            let made = |a: &Var, b: &Var| format!("Pair(a: {}, b: {})", a.name, b.name);
            let test = format!("{} == {}", made(&x, &y), made(&z, &w));
            choose(&test, x.value == z.value && y.value == w.value)
        }
    }
}

/// A program that holds numbers and does arithmetic and comparisons on
/// them, and builds no text at run time: it prints only what a decision
/// between fixed texts says.
pub fn quiet(g: &mut Gen) -> NumberCase {
    let vars = variables(g);
    let first = vars[0].ty;
    let mut source = format!("struct Pair {{\n  a: {0}\n  b: {0}\n}}\n\n", first.name);
    source.push_str("fn main() ! console {\n");
    for v in &vars {
        let spelling = spelled(g, v.value);
        source.push_str(&format!("  {}: {} = {spelling}\n", v.name, v.ty.name));
    }
    let mut stdout = String::new();
    for _ in 0..1 + g.below(8) {
        let (line, printed) = quiet_line(g, &vars);
        source.push_str(&format!("  {line}\n"));
        stdout.push_str(&format!("{printed}\n"));
    }
    source.push_str("}\n");
    NumberCase {
        source,
        stdout: stdout.into_bytes(),
        bound: vars.iter().map(|v| (v.ty, v.value)).collect(),
        mistake: None,
    }
}
