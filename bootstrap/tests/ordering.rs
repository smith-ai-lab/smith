//! The `Ord` that a struct, an enum, and a tuple are given from their parts.
//!
//! The generator builds structs and enums whose fields and cases hold whole
//! numbers, text, a `bool`, `()`, tuples, and the types built before them.
//! It builds two values of one type that are often alike, and works out how
//! they stand from the rule of `knowledge/language/automatic-traits.md`, with
//! the order of Rust on the numbers and the bytes of the text.

use std::cmp::Ordering;

use smith::scalar::{self, Op};
use smith::{lower, scalar::Outcome};

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "two_values_of_a_type_given_its_order_stand_as_its_parts_say",
        two_values_of_a_type_given_its_order_stand_as_its_parts_say,
    ),
    (
        "a_type_with_a_part_that_has_no_order_has_none_and_the_error_names_the_part",
        a_type_with_a_part_that_has_no_order_has_none_and_the_error_names_the_part,
    ),
    (
        "without_ord_takes_the_order_a_type_was_given_away",
        without_ord_takes_the_order_a_type_was_given_away,
    ),
    (
        "an_ord_a_program_writes_replaces_the_one_a_type_was_given",
        an_ord_a_program_writes_replaces_the_one_a_type_was_given,
    ),
    (
        "comparing_two_structs_of_whole_numbers_is_no_call",
        comparing_two_structs_of_whole_numbers_is_no_call,
    ),
];

/// The whole number types, and the values the generator gives each.
const INTS: &[(&str, &[i128])] = &[
    ("i64", &[-1, 0, 1, 2]),
    ("i8", &[-100, -1, 0, 100]),
    ("u8", &[0, 1, 200, 255]),
];

/// The texts: some are the start of another, and some differ by bytes that
/// are not one character each.
const TEXTS: &[&str] = &["", "a", "ab", "b", "é", "z", "日本"];

#[derive(Clone, Debug)]
enum Ty {
    Int(usize),
    Str,
    Bool,
    Unit,
    Def(usize),
    Tuple(Vec<Ty>),
}

#[derive(Clone, Debug)]
struct Def {
    name: String,
    is_enum: bool,
    /// The fields of a struct, or the cases of an enum with what each holds.
    items: Vec<(String, Option<Ty>)>,
}

#[derive(Clone, Debug)]
enum Val {
    Int(i128),
    Str(usize),
    Bool(bool),
    Unit,
    /// A struct's fields, or the case of an enum with what it holds.
    Def(Option<usize>, Vec<Val>),
    Tuple(Vec<Val>),
}

fn ty(g: &mut Gen, defs: usize, depth: usize) -> Ty {
    let kinds = if depth == 0 { 4 } else { 6 };
    match g.below(kinds) {
        0 => Ty::Int(g.below(INTS.len())),
        1 => Ty::Str,
        2 => Ty::Bool,
        3 => Ty::Unit,
        4 if defs > 0 => Ty::Def(g.below(defs)),
        _ => Ty::Tuple(
            (0..2 + g.below(2))
                .map(|_| match ty(g, defs, depth - 1) {
                    // A literal in a tuple is a `i64` unless a type decides.
                    Ty::Int(_) => Ty::Int(0),
                    t => t,
                })
                .collect(),
        ),
    }
}

fn definitions(g: &mut Gen) -> Vec<Def> {
    let names = ["Cat", "Pet", "Bag"];
    let mut defs: Vec<Def> = Vec::new();
    for name in names.iter().take(1 + g.below(3)) {
        let is_enum = g.one_in(2);
        let count = 1 + g.below(3);
        let items = (0..count)
            .map(|i| {
                let held = (!is_enum || g.below(3) > 0).then(|| ty(g, defs.len(), 1));
                (format!("{}{i}", if is_enum { "c" } else { "f" }), held)
            })
            .collect();
        defs.push(Def {
            name: name.to_string(),
            is_enum,
            items,
        });
    }
    defs
}

fn value(g: &mut Gen, defs: &[Def], t: &Ty) -> Val {
    match t {
        Ty::Int(k) => Val::Int(*g.pick(INTS[*k].1)),
        Ty::Str => Val::Str(g.below(TEXTS.len())),
        Ty::Bool => Val::Bool(g.one_in(2)),
        Ty::Unit => Val::Unit,
        Ty::Tuple(of) => Val::Tuple(of.iter().map(|t| value(g, defs, t)).collect()),
        Ty::Def(d) => {
            let def = &defs[*d];
            if def.is_enum {
                let case = g.below(def.items.len());
                let held = def.items[case].1.iter().map(|t| value(g, defs, t)).collect();
                Val::Def(Some(case), held)
            } else {
                let fields = def.items.iter().filter_map(|(_, t)| t.as_ref());
                Val::Def(None, fields.map(|t| value(g, defs, t)).collect())
            }
        }
    }
}

/// A value that keeps most of what `v` is, so that two values often agree in
/// their first parts.
fn similar(g: &mut Gen, defs: &[Def], t: &Ty, v: &Val) -> Val {
    if g.below(3) == 0 {
        return value(g, defs, t);
    }
    match (t, v) {
        (Ty::Tuple(of), Val::Tuple(vs)) => Val::Tuple(
            (of.iter().zip(vs))
                .map(|(t, v)| similar(g, defs, t, v))
                .collect(),
        ),
        (Ty::Def(d), Val::Def(case, vs)) => {
            let def = &defs[*d];
            let items: Vec<&Ty> = match case {
                Some(c) => def.items[*c].1.iter().collect(),
                None => def.items.iter().filter_map(|(_, t)| t.as_ref()).collect(),
            };
            Val::Def(
                *case,
                (items.into_iter().zip(vs))
                    .map(|(t, v)| similar(g, defs, t, v))
                    .collect(),
            )
        }
        _ => v.clone(),
    }
}

fn written_ty(defs: &[Def], t: &Ty) -> String {
    match t {
        Ty::Int(k) => INTS[*k].0.to_string(),
        Ty::Str => "str".to_string(),
        Ty::Bool => "bool".to_string(),
        Ty::Unit => "()".to_string(),
        Ty::Def(d) => defs[*d].name.clone(),
        Ty::Tuple(of) => {
            let items: Vec<String> = of.iter().map(|t| written_ty(defs, t)).collect();
            format!("({})", items.join(", "))
        }
    }
}

fn written(defs: &[Def], t: &Ty, v: &Val) -> String {
    match (t, v) {
        (_, Val::Int(n)) => n.to_string(),
        (_, Val::Str(s)) => format!("{:?}", TEXTS[*s]),
        (_, Val::Bool(b)) => b.to_string(),
        (_, Val::Unit) => "()".to_string(),
        (Ty::Tuple(of), Val::Tuple(vs)) => {
            let items: Vec<String> = (of.iter().zip(vs))
                .map(|(t, v)| written(defs, t, v))
                .collect();
            format!("({})", items.join(", "))
        }
        (Ty::Def(d), Val::Def(case, vs)) => {
            let def = &defs[*d];
            match case {
                Some(c) => {
                    let (name, held) = &def.items[*c];
                    match held {
                        Some(h) => format!(".{name}({})", written(defs, h, &vs[0])),
                        None => format!(".{name}"),
                    }
                }
                None => {
                    let fields = def.items.iter().filter_map(|(n, t)| Some((n, t.as_ref()?)));
                    let items: Vec<String> = (fields.zip(vs))
                        .map(|((n, t), v)| format!("{n}: {}", written(defs, t, v)))
                        .collect();
                    format!("{}({})", def.name, items.join(", "))
                }
            }
        }
        _ => unreachable!("a value of its type"),
    }
}

fn write_defs(defs: &[Def]) -> String {
    let mut out = String::new();
    for def in defs {
        let (keyword, body): (_, Vec<String>) = if def.is_enum {
            let cases = def.items.iter().map(|(n, t)| match t {
                Some(t) => format!("  {n}: {}", written_ty(defs, t)),
                None => format!("  {n}"),
            });
            ("enum", cases.collect())
        } else {
            let fields = def.items.iter().map(|(n, t)| {
                format!("  {n}: {}", written_ty(defs, t.as_ref().expect("a field")))
            });
            ("struct", fields.collect())
        };
        out.push_str(&format!("{keyword} {} {{\n{}\n}}\n\n", def.name, body.join("\n")));
    }
    out
}

/// How two values of the type stand: by the parts in order, by the order of
/// the cases and then what a case holds, and by the bytes of text.
fn stands(a: &Val, b: &Val) -> Ordering {
    let all = |x: &[Val], y: &[Val]| {
        (x.iter().zip(y))
            .map(|(x, y)| stands(x, y))
            .find(|o| o.is_ne())
            .unwrap_or(Ordering::Equal)
    };
    match (a, b) {
        (Val::Int(x), Val::Int(y)) => x.cmp(y),
        (Val::Str(x), Val::Str(y)) => TEXTS[*x].as_bytes().cmp(TEXTS[*y].as_bytes()),
        (Val::Bool(x), Val::Bool(y)) => x.cmp(y),
        (Val::Unit, Val::Unit) => Ordering::Equal,
        (Val::Tuple(x), Val::Tuple(y)) => all(x, y),
        (Val::Def(c, x), Val::Def(d, y)) => c.cmp(d).then_with(|| all(x, y)),
        _ => unreachable!("two values of one type"),
    }
}

struct Case {
    source: String,
    stdout: String,
}

impl std::fmt::Debug for Case {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}\nprints {:?}", self.source, self.stdout)
    }
}

/// A program that compares pairs of values with every operator, and what it
/// prints, which is for each pair the answers of `<`, `<=`, `>`, `>=`, `==`.
fn program(g: &mut Gen) -> Case {
    let defs = definitions(g);
    let mut source = write_defs(&defs);
    source.push_str("fn main() ! console, alloc {\n");
    let mut stdout = String::new();
    for i in 0..4 {
        let t = ty(g, defs.len(), 1);
        let a = value(g, &defs, &t);
        let b = similar(g, &defs, &t, &a);
        let name = written_ty(&defs, &t);
        source.push_str(&format!(
            "  a{i}: {name} = {}\n  b{i}: {name} = {}\n",
            written(&defs, &t, &a),
            written(&defs, &t, &b)
        ));
        source.push_str(&format!(
            "  print(\"{{a{i} < b{i}}} {{a{i} <= b{i}}} {{a{i} > b{i}}} {{a{i} >= b{i}}} {{a{i} == b{i}}}\")\n"
        ));
        let o = stands(&a, &b);
        stdout.push_str(&format!(
            "{} {} {} {} {}\n",
            o.is_lt(),
            o.is_le(),
            o.is_gt(),
            o.is_ge(),
            o.is_eq()
        ));
    }
    source.push_str("}\n");
    Case { source, stdout }
}

fn run(source: &str) -> Vec<u8> {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
    let Outcome { stdout, .. } = scalar::run(&program);
    stdout
}

fn two_values_of_a_type_given_its_order_stand_as_its_parts_say() {
    generate::check(234, 30, program, |case| {
        assert_eq!(
            String::from_utf8(run(&case.source)).unwrap(),
            case.stdout
        );
    });
}

/// The first of two compared values holds a `Key`, which has no `Ord`,
/// inside a struct, a tuple, or an enum case, as many times as `depth` says.
fn holding_a_key(g: &mut Gen) -> (String, bool) {
    let mut ty = "Key".to_string();
    let mut defs = String::from("struct Key {\n  secret: str\n}\n\nwithout Ord for Key\n\n");
    let mut value = "Key(secret: \"k\")".to_string();
    for level in 0..1 + g.below(3) {
        match g.below(3) {
            0 => {
                defs.push_str(&format!("struct S{level} {{\n  n: i64\n  held: {ty}\n}}\n\n"));
                value = format!("S{level}(n: 1, held: {value})");
                ty = format!("S{level}");
            }
            1 => {
                value = format!("(1, {value})");
                ty = format!("(i64, {ty})");
            }
            _ => {
                defs.push_str(&format!("enum E{level} {{\n  empty\n  full: {ty}\n}}\n\n"));
                value = format!(".full({value})");
                ty = format!("E{level}");
            }
        }
    }
    let source = format!(
        "{defs}fn main() ! console, alloc {{\n  a: {ty} = {value}\n  b: {ty} = {value}\n  if a < b {{ print(\"less\") }}\n}}\n"
    );
    (source, true)
}

fn a_type_with_a_part_that_has_no_order_has_none_and_the_error_names_the_part() {
    generate::check(235, 30, holding_a_key, |(source, _)| {
        let Err(found) = lower(source.as_bytes()) else {
            panic!("builds");
        };
        assert_eq!(found.len(), 1, "{found:?}");
        let message = &found[0].message;
        assert!(message.contains("Ord"), "{message}");
        assert!(message.contains("`Key`"), "{message}");
    });
}

fn without_ord_takes_the_order_a_type_was_given_away() {
    let generate = |g: &mut Gen| {
        let defs = definitions(g);
        let t = Ty::Def(g.below(defs.len()));
        let (a, b) = (value(g, &defs, &t), value(g, &defs, &t));
        let name = written_ty(&defs, &t);
        let main = format!(
            "fn main() ! console, alloc {{\n  a: {name} = {}\n  b: {name} = {}\n  if a < b {{ print(\"less\") }}\n}}\n",
            written(&defs, &t, &a),
            written(&defs, &t, &b)
        );
        (write_defs(&defs), main, name)
    };
    generate::check(236, 8, generate, |(defs, main, name)| {
        assert!(lower(format!("{defs}{main}").as_bytes()).is_ok());
        let source = format!("{defs}without Ord for {name}\n\n{main}");
        let Err(found) = lower(source.as_bytes()) else {
            panic!("builds");
        };
        let message = &found[0].message;
        assert!(message.contains("Ord") && message.contains(&format!("`{name}`")), "{message}");
    });
}

fn an_ord_a_program_writes_replaces_the_one_a_type_was_given() {
    generate::check(237, 20, |g: &mut Gen| (*g.pick(&[-1i64, 0, 1, 2]), *g.pick(&[-1i64, 0, 1, 2])), |&(x, y)| {
        let source = format!(
            "struct Rev {{\n  n: i64\n}}\n\nimpl Ord[Rev] {{\n  fn compare(a: Rev, b: Rev) -> Order {{ compare(b.n, a.n) }}\n}}\n\nfn main() ! console, alloc {{\n  a = (Rev(n: {x}), 0)\n  b = (Rev(n: {y}), 0)\n  print(\"{{a < b}} {{Rev(n: {x}) < Rev(n: {y})}}\")\n}}\n"
        );
        let expected = format!("{} {}\n", y < x, y < x);
        assert_eq!(String::from_utf8(run(&source)).unwrap(), expected, "{source}");
    });
}

fn comparing_two_structs_of_whole_numbers_is_no_call() {
    generate::check(238, 12, |g: &mut Gen| {
        let fields: Vec<&str> = (0..1 + g.below(3)).map(|_| INTS[g.below(INTS.len())].0).collect();
        fields
    }, |fields| {
        let list: Vec<String> = (fields.iter().enumerate())
            .map(|(i, t)| format!("  f{i}: {t}"))
            .collect();
        let zeros = vec!["0"; fields.len()];
        let values = |zeros: &[&str]| -> String {
            let items: Vec<String> = (zeros.iter().enumerate())
                .map(|(i, z)| format!("f{i}: {z}"))
                .collect();
            items.join(", ")
        };
        let source = format!(
            "struct S {{\n{}\n}}\n\nfn below(a: S, b: S) -> bool {{ a < b }}\n\nfn main() ! console {{\n  if below(S({}), S({})) {{ print(\"x\") }}\n}}\n",
            list.join("\n"),
            values(&zeros),
            values(&vec!["1"; fields.len()]),
        );
        let program = scalar::passes(&lower(source.as_bytes()).expect("compiles"));
        let calls = (program.functions.iter())
            .flat_map(|f| f.blocks.iter())
            .flat_map(|b| b.instructions.iter())
            .filter(|i| matches!(i.op, Op::Call { .. }))
            .count();
        assert_eq!(calls, 0, "in {source}");
    });
}
