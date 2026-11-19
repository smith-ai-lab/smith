//! A function with a type parameter takes and gives a `List[T]`, and a `str`
//! is given where it reads one, which makes `T` a `u8`.
//!
//! Every generator builds a generic function from a shape, and the same
//! function written out for one type, so the model of each is the other.

use std::collections::BTreeMap;

use smith::scalar::{self, Op};
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::programs::{lines_added, with_imports};

pub const ONCE: &[Test] = &[
    (
        "the_generic_functions_that_take_and_give_a_list_compile_and_give_what_they_say",
        the_generic_functions_that_take_and_give_a_list_compile_and_give_what_they_say,
    ),
    (
        "a_generic_function_over_lists_prints_what_the_function_written_for_one_type_prints",
        a_generic_function_over_lists_prints_what_the_function_written_for_one_type_prints,
    ),
    (
        "a_text_given_where_a_function_lends_a_list_is_a_list_of_bytes",
        a_text_given_where_a_function_lends_a_list_is_a_list_of_bytes,
    ),
    (
        "a_mistake_in_a_call_or_a_body_over_lists_is_one_error_at_its_place",
        a_mistake_in_a_call_or_a_body_over_lists_is_one_error_at_its_place,
    ),
    (
        "a_call_of_a_generic_function_on_a_list_of_bytes_is_the_code_of_the_function_for_bytes",
        a_call_of_a_generic_function_on_a_list_of_bytes_is_the_code_of_the_function_for_bytes,
    ),
];

/// What a list holds.
#[derive(Clone, Copy, Debug, PartialEq)]
enum Elem {
    U8,
    I32,
    U64,
    Text,
    Struct,
}

const ELEMS: &[Elem] = &[Elem::U8, Elem::I32, Elem::U64, Elem::Text, Elem::Struct];

impl Elem {
    fn name(self) -> &'static str {
        match self {
            Elem::U8 => "Byte",
            Elem::I32 => "i32",
            Elem::U64 => "u64",
            Elem::Text => "str",
            Elem::Struct => "P",
        }
    }

    /// The bounds a type has: `Ord` is for whole numbers.
    fn bounds(self) -> &'static [&'static str] {
        match self {
            Elem::Text | Elem::Struct => &["", ": Eq"],
            _ => &["", ": Eq", ": Ord"],
        }
    }

    fn value(self, n: usize) -> String {
        match self {
            Elem::Text => format!("\"t{n}\""),
            Elem::Struct => format!("P(a: {n})"),
            _ => n.to_string(),
        }
    }

    /// What prints an element named `x`.
    fn show(self, x: &str) -> String {
        match self {
            Elem::Struct => format!("print(\"{{{x}.a}}\")"),
            _ => format!("print(\"{{{x}}}\")"),
        }
    }
}

const STRUCT: &str = "struct P {\n  a: Byte\n}\n\n";

/// A shape of generic function: how it is written for the types `t` and `u`,
/// with `generics` before its parameters, what it needs of its first type
/// parameter and of its second, and what it gives.
#[derive(Clone, Copy, Debug)]
struct Shape {
    needs: (usize, usize),
    two: bool,
    gives: Gives,
}

#[derive(Clone, Copy, Debug)]
enum Gives {
    List,
    Bool,
    Item,
}

/// The bound of each number: none, `Eq`, `Ord`.
const NONE: usize = 0;
const EQ: usize = 1;
const ORD: usize = 2;

const SHAPES: &[Shape] = &[
    Shape { needs: (NONE, NONE), two: false, gives: Gives::List },
    Shape { needs: (NONE, NONE), two: false, gives: Gives::List },
    Shape { needs: (EQ, NONE), two: false, gives: Gives::Bool },
    Shape { needs: (EQ, NONE), two: false, gives: Gives::List },
    Shape { needs: (ORD, NONE), two: false, gives: Gives::List },
    Shape { needs: (NONE, NONE), two: false, gives: Gives::Item },
    Shape { needs: (NONE, NONE), two: true, gives: Gives::List },
    Shape { needs: (EQ, EQ), two: true, gives: Gives::List },
];

/// The source of function `name` of shape `k`, whose type parameters are
/// written as `t` and `u`, after `generics`.
fn function(k: usize, name: &str, generics: &str, t: &str, u: &str, mistake: bool) -> String {
    let push = if mistake { "true" } else { "b[i]" };
    let (params, gives, body) = match k {
        0 => (
            format!("a: take List[{t}], b: List[{t}]"),
            format!("List[{t}]"),
            format!("var r = a\n  loop i in 0..b.size {{ r = push(r, {push}) }}\n  r"),
        ),
        1 => (
            format!("a: List[{t}]"),
            format!("List[{t}]"),
            format!(
                "var r: List[{t}] = []\n  loop i in 0..a.size {{\n    r = push(r, a[i])\n    r = push(r, a[i])\n  }}\n  r"
            ),
        ),
        2 => (
            format!("a: List[{t}], y: {t}"),
            "bool".to_string(),
            "a.size > 0 and a[0] == y".to_string(),
        ),
        3 => (
            format!("a: List[{t}], y: {t}"),
            format!("List[{t}]"),
            format!(
                "var r: List[{t}] = []\n  loop i in 0..a.size {{ if a[i] == y {{ r = push(r, a[i]) }} }}\n  r"
            ),
        ),
        4 => (
            format!("a: List[{t}], y: {t}"),
            format!("List[{t}]"),
            format!(
                "var r: List[{t}] = []\n  loop i in 0..a.size {{ if a[i] < y {{ r = push(r, a[i]) }} }}\n  r"
            ),
        ),
        5 => (
            format!("a: List[{t}], y: {t}"),
            t.to_string(),
            "if a.size > 0 { a[0] } else { y }".to_string(),
        ),
        6 => (
            format!("a: take List[{t}], b: List[{u}], y: {t}"),
            format!("List[{t}]"),
            "var r = a\n  loop i in 0..b.size { r = push(r, y) }\n  r".to_string(),
        ),
        _ => (
            format!("a: List[{t}], b: List[{u}], y: {t}, z: {u}"),
            format!("List[{t}]"),
            format!(
                "var r: List[{t}] = []\n  loop i in 0..a.size {{\n    if a[i] == y and b.size > 0 and b[0] == z {{ r = push(r, a[i]) }}\n  }}\n  r"
            ),
        ),
    };
    let effects = if matches!(k, 2 | 5) { "" } else { " ! alloc" };
    // Nothing is known of a type parameter but what its bounds say, so each
    // is asked for `Copy` and `Discard`, as the body copies and drops.
    let asks: String = (generics.trim_matches(['[', ']']).split(", "))
        .filter(|g| !g.is_empty())
        .map(|g| g.split(':').next().unwrap_or(g))
        .map(|g| format!("\n  requires Copy[{g}]\n  requires Discard[{g}]"))
        .collect();
    let open = if asks.is_empty() { " {".to_string() } else { format!("{asks}\n{{") };
    format!("fn {name}{generics}({params}) -> {gives}{effects}{open}\n  {body}\n}}\n\n")
}

/// A generic function over lists, the arguments of its calls, and the types
/// they are for.
#[derive(Debug)]
struct Case {
    shape: usize,
    t: Elem,
    u: Elem,
    bound: (&'static str, &'static str),
    a: Vec<usize>,
    b: Vec<usize>,
    y: usize,
    z: usize,
}

fn case(g: &mut Gen) -> Case {
    let shape = g.below(SHAPES.len());
    let needs = SHAPES[shape].needs;
    let pick = |g: &mut Gen, need: usize| {
        let mut options: Vec<(Elem, &'static str)> = Vec::new();
        for &e in ELEMS {
            for (k, bound) in e.bounds().iter().enumerate() {
                if need == NONE || k == need {
                    options.push((e, bound));
                }
            }
        }
        *g.pick(&options)
    };
    let (t, tb) = pick(g, needs.0);
    let (u, ub) = pick(g, needs.1);
    let items = |g: &mut Gen| (0..g.below(5)).map(|_| g.below(4)).collect::<Vec<_>>();
    Case { shape, t, u, bound: (tb, ub), a: items(g), b: items(g), y: g.below(4), z: g.below(4) }
}

fn list(e: Elem, items: &[usize]) -> String {
    let items: Vec<String> = items.iter().map(|&n| e.value(n)).collect();
    format!("[{}]", items.join(", "))
}

impl Case {
    /// The arguments of a call, each list a fresh variable named `a`, `b`
    /// after `n`, since a list is used once.
    fn declare(&self, n: usize) -> String {
        let mut out = format!("  a{n}: List[{}] = {}\n", self.t.name(), list(self.t, &self.a));
        let second = if SHAPES[self.shape].two { self.u } else { self.t };
        out += &format!("  b{n}: List[{}] = {}\n", second.name(), list(second, &self.b));
        out
    }

    fn call(&self, name: &str, n: usize) -> String {
        let (y, z) = (self.t.value(self.y), self.u.value(self.z));
        let args = match self.shape {
            0 => format!("a{n}, b{n}"),
            1 => format!("a{n}"),
            2..=5 => format!("a{n}, {y}"),
            6 => format!("a{n}, b{n}, {y}"),
            _ => format!("a{n}, b{n}, {y}, {z}"),
        };
        format!("{name}({args})")
    }

    /// What prints what a call gives, with a line between the calls.
    fn show(&self, name: &str, n: usize) -> String {
        let call = self.call(name, n);
        match SHAPES[self.shape].gives {
            Gives::List => format!(
                "  r{n} = {call}\n  loop i in 0..r{n}.size {{ {} }}\n  print(\"--\")\n",
                self.t.show(&format!("r{n}[i]"))
            ),
            Gives::Bool => format!("  print(\"{{{call}}}\")\n  print(\"--\")\n"),
            Gives::Item => format!("  {}\n  print(\"--\")\n", self.t.show(&call)),
        }
    }

    /// A program that runs the generic function and the function written for
    /// the types, in that order, and prints what each gives.
    fn source(&self) -> String {
        let (t, u) = (self.t.name(), self.u.name());
        let two = SHAPES[self.shape].two;
        let generics = if two {
            format!("[T{}, U{}]", self.bound.0, self.bound.1)
        } else {
            format!("[T{}]", self.bound.0)
        };
        let mut s = STRUCT.to_string();
        s += &function(self.shape, "generic", &generics, "T", "U", false);
        s += &function(self.shape, "written", "", t, if two { u } else { t }, false);
        s += "fn main() ! console, alloc {\n";
        s += &self.declare(1);
        s += &self.show("generic", 1);
        s += &self.declare(2);
        s += &self.show("written", 2);
        s + "}\n"
    }
}

fn stdout(source: &str) -> String {
    let source = &with_imports(source);
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let out = scalar::run(&program);
    assert_eq!(out.status, 0, "for {source}");
    String::from_utf8(out.stdout).unwrap()
}

fn the_generic_functions_that_take_and_give_a_list_compile_and_give_what_they_say() {
    let append = "fn ap[T](a: take List[T], b: List[T]) -> List[T] ! alloc {\n  var r = a\n  loop i in 0..b.size { r = push(r, b[i]) }\n  r\n}\n\n";
    let eq = "fn e[T: Eq](x: List[T], y: T) -> bool { x.size > 0 and x[0] == y }\n\n";
    let n = "fn n[T](x: List[T]) -> Nat { x.size }\n\n";
    let main = "fn main() ! console, alloc {\n  a: List[Byte] = [1, 2]\n  b: List[Byte] = [3]\n  loop i in 0..ap(a, b).size { print(\"x\") }\n  c: List[Byte] = [5]\n  d: List[Byte] = [1, 2]\n  l: List[Byte] = ap(c, d)\n  loop i in 0..l.size { print(\"{l[i]}\") }\n  e1: List[Byte] = [7, 8]\n  e2: List[Byte] = [7]\n  print(\"{e(e1, 7)} {e(e2, 9)}\")\n  print(\"{n(\"abc\")}\")\n  f: List[Byte] = [9]\n  g: List[Byte] = ap([4, 5], f)\n  print(\"{g.size}\")\n}\n";
    assert_eq!(stdout(&format!("{append}{eq}{n}{main}")), "x\nx\nx\n5\n1\n2\ntrue false\n3\n3\n");
}

fn a_generic_function_over_lists_prints_what_the_function_written_for_one_type_prints() {
    generate::check(361, 40, case, |c| {
        let out = stdout(&c.source());
        let (generic, written) = out.split_once("--\n").unwrap();
        assert_eq!(format!("{generic}--\n"), format!("{}", written), "for {}", c.source());
    });
}

/// A text, as the list of the bytes it is.
#[derive(Debug)]
struct Text {
    shape: usize,
    bound: &'static str,
    a: String,
    b: String,
    y: u8,
}

fn text(g: &mut Gen) -> Text {
    let shape = *g.pick(&[0usize, 1, 3, 5]);
    let word = |g: &mut Gen| (0..g.below(5)).map(|_| (b'a' + g.below(4) as u8) as char).collect();
    let bounds: &[&str] = match SHAPES[shape].needs.0 {
        NONE => &["", ": Eq", ": Ord"],
        EQ => &[": Eq"],
        _ => &[": Ord"],
    };
    Text { shape, bound: g.pick(bounds), a: word(g), b: word(g), y: b'a' + g.below(4) as u8 }
}

fn bytes(s: &str) -> String {
    let items: Vec<String> = s.bytes().map(|b| b.to_string()).collect();
    format!("[{}]", items.join(", "))
}

fn a_text_given_where_a_function_lends_a_list_is_a_list_of_bytes() {
    generate::check(362, 24, text, |c| {
        let f = function(c.shape, "f", &format!("[T{}]", c.bound), "T", "T", false);
        let args = |a: &str, b: &str| match c.shape {
            0 => format!("{a}, {b}"),
            1 => a.to_string(),
            _ => format!("{a}, {}", c.y),
        };
        let shown = |what: &str| match SHAPES[c.shape].gives {
            Gives::List => format!("  r = {what}\n  loop i in 0..r.size {{ print(\"{{r[i]}}\") }}\n"),
            _ => format!("  print(\"{{{what}}}\")\n"),
        };
        let (a, b) = (format!("\"{}\"", c.a), format!("\"{}\"", c.b));
        let as_text = shown(&format!("f({})", args(&a, &b)));
        let declare = format!("  xs: List[Byte] = {}\n  ys: List[Byte] = {}\n", bytes(&c.a), bytes(&c.b));
        let as_list = shown(&format!("f({})", args("xs", "ys")));
        let one = |declare: &str, body: &str| format!("{f}fn main() ! console, alloc {{\n{declare}{body}}}\n");
        assert_eq!(stdout(&one("", &as_text)), stdout(&one(&declare, &as_list)), "for {c:?}");
    });
}

/// A mistake and where it is: the source has `\u{1}` before the place.
#[derive(Debug)]
struct Mistake {
    source: String,
}

fn mistake(g: &mut Gen) -> Mistake {
    let ty = g.pick(&["Byte", "i32", "u64"]);
    let other = g.pick(&["u16", "Int", "bool"]);
    let words = ["ab", "", "xyz"];
    let word = g.pick(&words);
    let main = |body: &str| format!("fn main() ! console, alloc {{\n{body}}}\n");
    let append = |generic: bool, wrong: bool| {
        function(0, "ap", if generic { "[T]" } else { "" }, if generic { "T" } else { ty }, "U", wrong)
    };
    let source = match g.below(5) {
        0 => format!(
            "{}{}",
            append(true, true),
            main(&format!(
                "  xs: List[{ty}] = [1]\n  ys: List[{ty}] = [2]\n  zs = ap(xs, ys)\n"
            ))
        )
        .replace("true", "\u{1}true"),
        1 => format!(
            "{}{}",
            append(true, false),
            main(&format!("  ys: List[{other}] = []\n  zs = ap(\"{word}\", \u{1}ys)\n"))
        ),
        4 => format!(
            "{}{}",
            append(true, false),
            main(&format!("  ys: List[{other}] = []\n  zs = ap(ys, \u{1}\"{word}\")\n"))
        ),
        2 => format!(
            "{}{}",
            append(true, false),
            main("  zs = ap(\u{1}[1, 2], [3])\n")
        ),
        _ => format!(
            "{}{}",
            append(true, false),
            main("  zs = ap(\u{1}[], [])\n")
        ),
    };
    // The text is a `List[u8]`, so a list of `u8` accepts it.
    Mistake { source }
}

fn a_mistake_in_a_call_or_a_body_over_lists_is_one_error_at_its_place() {
    generate::check(363, 40, mistake, |m| {
        let at = m.source.find('\u{1}').unwrap();
        let before = &m.source[..at];
        let (line, col) = (before.matches('\n').count() + 1, at - before.rfind('\n').map_or(0, |n| n + 1) + 1);
        let written = m.source.replace('\u{1}', "");
        // The `use` lines of what the program writes come first.
        let source = with_imports(&written);
        let line = line + lines_added(&written, &source) as usize;
        let errors = check(source.as_bytes()).expect_err(&format!("compiled: {source}"));
        let places: Vec<(usize, usize)> = errors.iter().map(|e| (e.pos.line, e.pos.col)).collect();
        assert_eq!(places, vec![(line, col)], "{errors:?} for {source}");
    });
}

fn kinds(source: &str) -> BTreeMap<String, usize> {
    let source = &with_imports(source);
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let mut counts = BTreeMap::new();
    for i in (scalar::inline(&program).functions.iter())
        .flat_map(|f| &f.blocks)
        .flat_map(|b| &b.instructions)
    {
        let kind = match &i.op {
            Op::Call { .. } => "Call".to_string(),
            op => format!("{:?}", std::mem::discriminant(op)),
        };
        *counts.entry(kind).or_insert(0) += 1;
    }
    counts
}

fn a_call_of_a_generic_function_on_a_list_of_bytes_is_the_code_of_the_function_for_bytes() {
    for shape in [0, 1, 3, 4] {
        let bound = if shape == 3 { ": Eq" } else if shape == 4 { ": Ord" } else { "" };
        let args = match shape {
            0 => "xs, ys",
            1 => "xs",
            _ => "xs, 3",
        };
        let program = |function: String, name: &str| {
            format!(
                "{function}fn main() ! console, alloc {{\n  xs: List[Byte] = [1, 2]\n  ys: List[Byte] = [3]\n  r = {name}({args})\n  print(\"{{r.size}}\")\n}}\n"
            )
        };
        let generic = program(function(shape, "f", &format!("[T{bound}]"), "T", "T", false), "f");
        let written = program(function(shape, "f", "", "Byte", "Byte", false), "f");
        assert_eq!(kinds(&generic), kinds(&written), "for {generic}");
    }
}
