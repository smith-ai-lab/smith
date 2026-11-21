//! A text built by interpolation lasts as long as anything holds it. Each
//! program builds texts, passes them through calls whose arguments come in any
//! order, with constants among them, stores them in a struct, a list, a tuple,
//! or an enum case that a function gives back, builds other texts, and prints
//! the first ones later. The expectation of each is what the generator that
//! wrote it computed by the rule.

use smith::scalar::{self, Outcome};
use smith::lower;

use crate::Test;
use crate::generate::{self, Gen};
use crate::targets;

pub const ONCE: &[Test] = &[(
    "a_text_built_by_interpolation_and_passed_through_calls_is_read_later_as_it_was_built",
    a_text_built_by_interpolation_and_passed_through_calls_is_read_later_as_it_was_built,
)];

/// The executables of the programs are checked against the evaluator.
pub const MACHINE: &[Test] = &[(
    "every_executable_reads_texts_built_by_interpolation_as_the_evaluator_does",
    every_executable_reads_texts_built_by_interpolation_as_the_evaluator_does,
)];

fn outcome(source: &str) -> Outcome {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
    scalar::run(&program)
}

/// Where each of the parts a value holds comes from.
#[derive(Clone, Copy, Debug)]
enum Source {
    /// A literal text.
    Literal,
    /// A text built by interpolation from the number the function is given.
    Built,
}

/// What a generated program holds the texts in.
#[derive(Clone, Copy, Debug)]
enum Shape {
    Struct,
    List,
    Tuple,
    Enum,
}

#[derive(Clone, Debug)]
struct Case {
    shape: Shape,
    /// The order of the parameters of the function that stores the parts: the
    /// number, the tag, and the body.
    order: [usize; 3],
    tag: Source,
    body: Source,
    /// How many functions the parts pass through before they are stored.
    through: usize,
    /// Whether the later reading is in a function of its own.
    apart: bool,
    numbers: Vec<usize>,
}

fn case(g: &mut Gen) -> Case {
    let mut order = [0, 1, 2];
    for i in (1..3).rev() {
        order.swap(i, g.below(i + 1));
    }
    let source = |g: &mut Gen| if g.one_in(2) { Source::Literal } else { Source::Built };
    Case {
        shape: *g.pick(&[Shape::Struct, Shape::List, Shape::Tuple, Shape::Enum]),
        order,
        tag: source(g),
        body: source(g),
        through: g.below(3),
        apart: g.one_in(2),
        numbers: (0..3).map(|_| g.below(100)).collect(),
    }
}

const PARAMS: [(&str, &str); 3] = [("n", "u64"), ("tag", "str"), ("body", "str")];

fn text_of(source: Source, built: &str, literal: &str) -> String {
    match source {
        Source::Literal => format!("\"{literal}\""),
        Source::Built => format!("\"{built}\""),
    }
}

/// The program, and what it prints.
fn program(c: &Case) -> (String, String) {
    let mut s = String::from("use std.bits (u64)\nuse std.console (console, print)\nuse std.list (push)\n\n");
    let list = matches!(c.shape, Shape::List);
    let held = match c.shape {
        Shape::Struct => "struct Held {\n  n: u64\n  tag: str\n  body: str\n}\n\n",
        Shape::List => "",
        Shape::Tuple => "",
        Shape::Enum => "enum Held {\n  one: (u64, str, str)\n  none\n}\n\n",
    };
    s += held;
    let ty = match c.shape {
        Shape::Struct | Shape::Enum => "Held",
        Shape::List => "List[str]",
        Shape::Tuple => "(u64, str, str)",
    };
    let params = |names: &str| {
        c.order
            .iter()
            .map(|&i| format!("{}: {}", PARAMS[i].0, PARAMS[i].1))
            .collect::<Vec<_>>()
            .join(", ")
            + names
    };
    let store = match c.shape {
        Shape::Struct => "Held(n: n, tag: tag, body: body)".to_string(),
        Shape::List => "push(push([], tag), body)".to_string(),
        Shape::Tuple => "(n, tag, body)".to_string(),
        Shape::Enum => "Held.one((n, tag, body))".to_string(),
    };
    s += &format!("fn store({}) -> {ty} ! alloc {{\n  {store}\n}}\n\n", params(""));
    let mut inner = "store".to_string();
    for level in 0..c.through {
        let name = format!("pass{level}");
        let args = c.order.iter().map(|&i| PARAMS[i].0).collect::<Vec<_>>().join(", ");
        s += &format!("fn {name}({}) -> {ty} ! alloc {{\n  {inner}({args})\n}}\n\n", params(""));
        inner = name;
    }
    let arg = |i: usize| match i {
        0 => "k".to_string(),
        1 => text_of(c.tag, "t{k}", "tag"),
        _ => text_of(c.body, "v{k}-{k}", "body"),
    };
    let args = c.order.iter().map(|&i| arg(i)).collect::<Vec<_>>().join(", ");
    s += &format!("fn make(k: u64) -> {ty} ! alloc {{\n  {inner}({args})\n}}\n\n");
    let read = match c.shape {
        Shape::Struct => "print(\"{h.n} {h.tag} {h.body}\")".to_string(),
        Shape::List => "if h.size == 2 {\n    print(\"{h[0]} {h[1]}\")\n  }".to_string(),
        Shape::Tuple => "(n, tag, body) = h\n  print(\"{n} {tag} {body}\")".to_string(),
        Shape::Enum => "match h {\n    .one(t) {\n      (n, tag, body) = t\n      print(\"{n} {tag} {body}\")\n    }\n    .none { }\n  }".to_string(),
    };
    let reads = if c.apart {
        s += &format!("fn show(h: {ty}) ! console, alloc {{\n  junk = \"zz{{9}}zz\"\n  print(junk)\n  {read}\n}}\n\n");
        "show(h)".to_string()
    } else {
        format!("junk = \"zz{{9}}zz\"\n  print(junk)\n  {read}")
    };
    s += &format!(
        "fn step(k: u64) ! console, alloc {{\n  h = make(k)\n  other = \"o{{k}}\"\n  print(other)\n  {reads}\n}}\n\n"
    );
    s += "fn main() ! console, alloc {\n";
    let mut said = String::new();
    for n in &c.numbers {
        s += &format!("  step({n})\n");
        said += &format!("o{n}\n");
        said += "zz9zz\n";
        let tag = match c.tag {
            Source::Literal => "tag".to_string(),
            Source::Built => format!("t{n}"),
        };
        let body = match c.body {
            Source::Literal => "body".to_string(),
            Source::Built => format!("v{n}-{n}"),
        };
        said += &if list {
            format!("{tag} {body}\n")
        } else {
            format!("{n} {tag} {body}\n")
        };
    }
    s += "}\n";
    (s, said)
}

fn a_text_built_by_interpolation_and_passed_through_calls_is_read_later_as_it_was_built() {
    generate::check(8601, 20, case, |c| {
        let (source, said) = program(c);
        assert_eq!(
            String::from_utf8(outcome(&source).stdout).unwrap(),
            said,
            "for\n{source}"
        );
    });
}

fn every_executable_reads_texts_built_by_interpolation_as_the_evaluator_does() {
    generate::check(8602, 6, case, |c| {
        let (source, said) = program(c);
        let ran = outcome(&source);
        assert_eq!(String::from_utf8(ran.stdout.clone()).unwrap(), said, "for\n{source}");
        for target in targets::runnable() {
            targets::runs_as(source.as_bytes(), target, &ran);
        }
    });
}
