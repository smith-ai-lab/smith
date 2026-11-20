//! The generator of programs that print structs, enums, and tuples with the
//! `Fmt` and `Repr` they are given, which can also place one mistake in
//! what it builds.
//!
//! A program defines a few structs and enums, each of which may have type
//! parameters, and whose fields and cases hold text, a type parameter, an
//! `Option`, a tuple, or a struct or an enum defined before it. `main` binds
//! values of such types, with any nesting and type arguments, to names
//! whose types it states, and prints each with `Fmt`, with `Repr`, and
//! through functions whose bounds ask for them. A struct or an enum without
//! type parameters may implement `Fmt` or `Repr` itself.
//!
//! The generator works out what the program prints from the values it
//! built, as `knowledge/language/repr.md` says each is written.

use std::cell::Cell;

use crate::generate::Gen;
use crate::programs::{Expected, PIECES, pos_after};

/// A program that prints values with `Fmt` and `Repr`: its source, what it
/// prints, and the mistake placed in it, if any.
pub struct ReprCase {
    pub source: String,
    pub stdout: Vec<u8>,
    pub mistake: Option<Expected>,
}

impl std::fmt::Debug for ReprCase {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self.source)
    }
}

/// A program whose values are built twice: once from the expressions the
/// generator wrote, and once from text put in their place.
pub struct Written {
    /// The source with `HOLE` in place of the expression each name is bound
    /// to, which is the next of `values`.
    template: String,
    pub values: Vec<String>,
    /// How many lines each value is printed on.
    pub lines: Vec<usize>,
}

impl Written {
    /// The source with these expressions in place of the holes.
    pub fn with(&self, values: &[String]) -> String {
        let mut out = String::new();
        let mut rest = self.template.as_str();
        for v in values {
            let at = rest.find(HOLE).expect("a hole for each value");
            out.push_str(&rest[..at]);
            out.push_str(v);
            rest = &rest[at + HOLE.len()..];
        }
        out.push_str(rest);
        out
    }
}

impl std::fmt::Debug for Written {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self.with(&self.values))
    }
}

const HOLE: &str = "\u{0}";

/// What every program starts with: every one prints.
const HEADER: &str = "use std.console (console, print)\n\n";

const NAMES: &[&str] = &["Cat", "Pet", "Bag", "Duo"];
const FIELDS: &[&str] = &["name", "left", "right"];
const CASES: &[&str] = &["cat", "dog", "none"];
const PARAMS: &[&str] = &["T", "U"];

thread_local! {
    /// Whether the types the generator builds may be a `List`, which the
    /// programs of `program_with_lists` have and the others, whose tests ask
    /// what a program of texts and structs does, do not.
    static LISTS: Cell<bool> = const { Cell::new(false) };
}

/// Lets the generator build lists until it is dropped.
struct WithLists;

impl WithLists {
    fn new() -> Self {
        LISTS.with(|l| l.set(true));
        WithLists
    }
}

impl Drop for WithLists {
    fn drop(&mut self) {
        LISTS.with(|l| l.set(false));
    }
}

fn lists() -> bool {
    LISTS.with(Cell::get)
}

/// How deep a type of a field, a case, or a value is built.
const DEPTH: usize = 2;
/// The longest text a value is written as, so that a run stays short.
const MOST: usize = 600;
/// The largest type a value is bound to, as `Ty::size` counts it. The compiler
/// checks a program in time that grows with the size of its types, about 40
/// microseconds for each, and the size grows about eight times for each level
/// of `Cat[Cat[..]]`, so a program whose types are not bounded can take it
/// longer than a test may compute, whatever the program prints. A type of 400
/// takes it under 20 milliseconds, and the size of one the generator builds
/// is 1 to 350 in all but one case in a hundred.
const LARGEST: usize = 400;

#[derive(Clone, Debug, PartialEq, Eq)]
enum Ty {
    Str,
    Param(usize),
    /// A struct or an enum the program defines, with its type arguments.
    Def(usize, Vec<Ty>),
    Option(Box<Ty>),
    Tuple(Vec<Ty>),
    /// A `List` of a type.
    List(Box<Ty>),
    /// `Hidden`, a struct from which a mistake takes `Fmt` or `Repr` away.
    Hidden,
}

impl Ty {
    fn subst(&self, args: &[Ty]) -> Ty {
        match self {
            Ty::Param(p) => args[*p].clone(),
            Ty::Def(d, of) => Ty::Def(*d, of.iter().map(|t| t.subst(args)).collect()),
            Ty::Option(t) => Ty::Option(Box::new(t.subst(args))),
            Ty::List(t) => Ty::List(Box::new(t.subst(args))),
            Ty::Tuple(of) => Ty::Tuple(of.iter().map(|t| t.subst(args)).collect()),
            t => t.clone(),
        }
    }

    /// How many types the type is made of once each struct and enum in it
    /// is written out as what it holds. A type argument that a definition
    /// holds more than once is counted each time, so the size grows with the
    /// product of the definitions that hold their parameter many times, not
    /// with how the type is written.
    fn size(&self, defs: &[Def]) -> usize {
        match self {
            Ty::Def(d, args) => {
                let held: usize = (defs[*d].items.iter())
                    .filter_map(|(_, t)| t.as_ref())
                    .map(|t| t.subst(args).size(defs))
                    .sum();
                1 + held
            }
            Ty::Option(t) | Ty::List(t) => 1 + t.size(defs),
            Ty::Tuple(of) => 1 + of.iter().map(|t| t.size(defs)).sum::<usize>(),
            Ty::Str | Ty::Param(_) | Ty::Hidden => 1,
        }
    }

    fn mentions(&self, p: usize) -> bool {
        match self {
            Ty::Param(q) => *q == p,
            Ty::Def(_, of) | Ty::Tuple(of) => of.iter().any(|t| t.mentions(p)),
            Ty::Option(t) | Ty::List(t) => t.mentions(p),
            Ty::Str | Ty::Hidden => false,
        }
    }

    /// How the type is written, in the source and in an error.
    fn written(&self, defs: &[Def]) -> String {
        let list = |of: &[Ty]| -> String {
            let items: Vec<String> = of.iter().map(|t| t.written(defs)).collect();
            items.join(", ")
        };
        match self {
            Ty::Str => "str".to_string(),
            Ty::Param(p) => PARAMS[*p].to_string(),
            Ty::Def(d, of) if of.is_empty() => defs[*d].name.clone(),
            Ty::Def(d, of) => format!("{}[{}]", defs[*d].name, list(of)),
            Ty::Option(t) => format!("Option[{}]", t.written(defs)),
            Ty::List(t) => format!("List[{}]", t.written(defs)),
            Ty::Tuple(of) => format!("({})", list(of)),
            Ty::Hidden => "Hidden".to_string(),
        }
    }
}

#[derive(Clone, Debug)]
struct Def {
    name: String,
    is_enum: bool,
    params: usize,
    /// The fields of a struct, or the cases of an enum with what each
    /// holds.
    items: Vec<(String, Option<Ty>)>,
    /// The text its own `Fmt` and `Repr` give, as pieces of a literal, if
    /// it implements them itself.
    fmt: Option<Vec<usize>>,
    repr: Option<Vec<usize>>,
}

/// A value, with the expression that builds it where its type is known.
#[derive(Clone, Debug)]
enum Val {
    /// The pieces of a literal.
    Str(Vec<usize>),
    /// A value of a struct, or a case of an enum with what it holds.
    Def(usize, Option<usize>, Vec<Val>),
    Some(Box<Val>),
    None,
    Tuple(Vec<Val>),
    /// The items of a list.
    List(Vec<Val>),
    /// A value of `Hidden`.
    Hidden,
}

fn pieces(g: &mut Gen) -> Vec<usize> {
    (0..g.below(3)).map(|_| g.below(PIECES.len())).collect()
}

fn text_of(pieces: &[usize]) -> Vec<u8> {
    pieces.iter().flat_map(|&p| PIECES[p].1.bytes()).collect()
}

fn literal(pieces: &[usize]) -> String {
    let inner: String = pieces.iter().map(|&p| PIECES[p].0).collect();
    format!("\"{inner}\"")
}

/// A text written as a string literal, as `interpolation.md` lists its escapes.
fn escaped(text: &[u8]) -> Vec<u8> {
    let mut out = vec![b'"'];
    for &b in text {
        match b {
            b'"' | b'\\' | b'{' | b'}' => out.extend([b'\\', b]),
            b'\n' => out.extend(b"\\n"),
            b'\t' => out.extend(b"\\t"),
            b => out.push(b),
        }
    }
    out.push(b'"');
    out
}

/// A type among `params` type parameters, that names only the definitions
/// before `defs.len()`.
fn ty(g: &mut Gen, defs: &[Def], params: usize, depth: usize) -> Ty {
    let mut choices = vec![0];
    if params > 0 {
        choices.push(1);
    }
    if depth > 0 {
        choices.extend([2, 3]);
        if lists() {
            choices.push(5);
        }
        if !defs.is_empty() {
            choices.extend([4, 4]);
        }
    }
    match *g.pick(&choices) {
        0 => Ty::Str,
        1 => Ty::Param(g.below(params)),
        2 => Ty::Option(Box::new(ty(g, defs, params, depth - 1))),
        3 => Ty::Tuple(
            (0..2 + g.below(2))
                .map(|_| ty(g, defs, params, depth - 1))
                .collect(),
        ),
        5 => Ty::List(Box::new(ty(g, defs, params, depth - 1))),
        _ => {
            let d = g.below(defs.len());
            let args = (0..defs[d].params)
                .map(|_| ty(g, defs, params, depth - 1))
                .collect();
            Ty::Def(d, args)
        }
    }
}

fn definitions(g: &mut Gen, overrides: bool) -> Vec<Def> {
    let mut defs: Vec<Def> = Vec::new();
    for name in NAMES.iter().take(1 + g.below(NAMES.len())) {
        let params = if g.one_in(2) { 0 } else { 1 + g.below(2) };
        let is_enum = g.one_in(2);
        let names = if is_enum { CASES } else { FIELDS };
        let count = if is_enum {
            1 + g.below(names.len())
        } else {
            g.below(names.len() + 1)
        };
        let mut items: Vec<(String, Option<Ty>)> = (names.iter().take(count))
            .map(|n| {
                let held = !is_enum || !g.one_in(3);
                (n.to_string(), held.then(|| ty(g, &defs, params, DEPTH)))
            })
            .collect();
        // Every type parameter is named by a field or a case, so the
        // arguments of a value's type say what it holds.
        for p in 0..params {
            if !items
                .iter()
                .any(|(_, t)| t.as_ref().is_some_and(|t| t.mentions(p)))
            {
                let at = g.below(items.len() + 1);
                let item = (format!("of{p}"), Some(Ty::Param(p)));
                items.insert(at, item);
            }
        }
        let fmt = (overrides && params == 0 && g.one_in(4)).then(|| pieces(g));
        let repr = (overrides && params == 0 && g.one_in(4)).then(|| pieces(g));
        defs.push(Def {
            name: name.to_string(),
            is_enum,
            params,
            items,
            fmt,
            repr,
        });
    }
    defs
}

/// A type with no type parameter, for a value to print.
fn concrete(g: &mut Gen, defs: &[Def]) -> Ty {
    if g.one_in(4) {
        return ty(g, defs, 0, DEPTH);
    }
    let d = g.below(defs.len());
    let args = (0..defs[d].params).map(|_| ty(g, defs, 0, 1)).collect();
    Ty::Def(d, args)
}

fn value(g: &mut Gen, defs: &[Def], t: &Ty) -> Val {
    match t {
        Ty::Str => Val::Str(pieces(g)),
        Ty::Option(inner) if g.one_in(2) => Val::Some(Box::new(value(g, defs, inner))),
        Ty::Option(_) => Val::None,
        Ty::Tuple(of) => Val::Tuple(of.iter().map(|t| value(g, defs, t)).collect()),
        Ty::List(of) => Val::List((0..g.below(4)).map(|_| value(g, defs, of)).collect()),
        Ty::Def(d, args) => {
            let def = &defs[*d];
            let holds = |t: &Option<Ty>| t.as_ref().map(|t| t.subst(args));
            if def.is_enum {
                let c = g.below(def.items.len());
                let held = holds(&def.items[c].1).map(|t| value(g, defs, &t));
                Val::Def(*d, Some(c), held.into_iter().collect())
            } else {
                let fields = (def.items.iter())
                    .map(|(_, t)| value(g, defs, &holds(t).expect("a field")))
                    .collect();
                Val::Def(*d, None, fields)
            }
        }
        Ty::Hidden => Val::Hidden,
        Ty::Param(_) => unreachable!("a value of a concrete type"),
    }
}

/// The expression that builds the value, where its type is known.
fn expr(defs: &[Def], v: &Val) -> String {
    let list = |of: &[Val]| -> String {
        let items: Vec<String> = of.iter().map(|v| expr(defs, v)).collect();
        items.join(", ")
    };
    match v {
        Val::Str(p) => literal(p),
        Val::Def(d, None, fields) => {
            let def = &defs[*d];
            let fields: Vec<String> = (def.items.iter().zip(fields))
                .map(|((n, _), v)| format!("{n}: {}", expr(defs, v)))
                .collect();
            format!("{}({})", def.name, fields.join(", "))
        }
        Val::Def(d, Some(c), held) => {
            let name = &defs[*d].items[*c].0;
            match held.as_slice() {
                [] => format!(".{name}"),
                held => format!(".{name}({})", list(held)),
            }
        }
        Val::Some(v) => format!(".some({})", expr(defs, v)),
        Val::None => ".none".to_string(),
        Val::Tuple(of) => format!("({})", list(of)),
        Val::List(of) => format!("[{}]", list(of)),
        Val::Hidden => "Hidden(key: \"k\")".to_string(),
    }
}

/// The text `Repr` gives for the value.
fn repr(defs: &[Def], v: &Val) -> Vec<u8> {
    let list = |of: &[Val], labels: Vec<String>| -> Vec<u8> {
        let items: Vec<Vec<u8>> = (of.iter().zip(labels))
            .map(|(v, label)| [label.into_bytes(), repr(defs, v)].concat())
            .collect();
        items.join(&b", "[..])
    };
    match v {
        Val::Str(p) => escaped(&text_of(p)),
        Val::Def(d, _, _) if defs[*d].repr.is_some() => text_of(defs[*d].repr.as_ref().unwrap()),
        Val::Def(d, None, fields) => {
            let def = &defs[*d];
            let labels = def.items.iter().map(|(n, _)| format!("{n}: ")).collect();
            [
                format!("{}(", def.name).into_bytes(),
                list(fields, labels),
                b")".to_vec(),
            ]
            .concat()
        }
        Val::Def(d, Some(c), held) => {
            let name = &defs[*d].items[*c].0;
            match held.as_slice() {
                [] => format!(".{name}").into_bytes(),
                held => [
                    format!(".{name}(").into_bytes(),
                    list(held, vec![String::new()]),
                    b")".to_vec(),
                ]
                .concat(),
            }
        }
        Val::Some(v) => [b".some(".to_vec(), repr(defs, v), b")".to_vec()].concat(),
        Val::None => b".none".to_vec(),
        Val::Tuple(of) => {
            let labels = vec![String::new(); of.len()];
            [b"(".to_vec(), list(of, labels), b")".to_vec()].concat()
        }
        Val::List(of) => [b"[".to_vec(), list(of, vec![String::new(); of.len()]), b"]".to_vec()].concat(),
        Val::Hidden => unreachable!("a mistake is not run"),
    }
}

/// The text `Fmt` gives for the value: the text of a `str`, that of a
/// type's own `Fmt`, and otherwise its `Repr`.
fn fmt(defs: &[Def], v: &Val) -> Vec<u8> {
    match v {
        Val::Str(p) => text_of(p),
        Val::Def(d, _, _) if defs[*d].fmt.is_some() => text_of(defs[*d].fmt.as_ref().unwrap()),
        v => repr(defs, v),
    }
}

/// The way `main` prints a value.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Print {
    /// `print("{x}")`.
    Fmt,
    /// `print(repr(x))`.
    Repr,
    /// `print("{repr(x)}")`, through `Fmt` of a `str`.
    ReprInBraces,
    /// `print(shown(x))`, which puts it in braces in a function whose
    /// bound is `Fmt`.
    Shown,
    /// `print(written(x))`, which calls `repr` in a function whose bound is
    /// `Repr`.
    Written,
}

const PRINTS: &[Print] = &[
    Print::Fmt,
    Print::Repr,
    Print::ReprInBraces,
    Print::Shown,
    Print::Written,
];

struct Model {
    defs: Vec<Def>,
    /// Each value `main` binds, with its type, and how it is printed.
    values: Vec<(Ty, Val, Vec<Print>)>,
}

fn model(g: &mut Gen, overrides: bool, prints: &[Print]) -> Model {
    loop {
        let defs = definitions(g, overrides);
        let mut values = Vec::new();
        for _ in 0..1 + g.below(3) {
            let t = concrete(g, &defs);
            let v = value(g, &defs, &t);
            let ways = (0..1 + g.below(2)).map(|_| *g.pick(prints)).collect();
            values.push((t, v, ways));
        }
        let m = Model { defs, values };
        if (m.values.iter()).all(|(t, v, _)| {
            t.size(&m.defs) <= LARGEST && repr(&m.defs, v).len() <= MOST
        }) {
            return m;
        }
    }
}

fn write_defs(m: &Model, out: &mut String) {
    for def in &m.defs {
        let kind = if def.is_enum { "enum" } else { "struct" };
        out.push_str(&format!("{kind} {}", def.name));
        if def.params > 0 {
            out.push_str(&format!("[{}]", PARAMS[..def.params].join(", ")));
        }
        out.push_str(" {\n");
        for (name, t) in &def.items {
            match t {
                Some(t) => out.push_str(&format!("  {name}: {}\n", t.written(&m.defs))),
                None => out.push_str(&format!("  {name}\n")),
            }
        }
        out.push_str("}\n\n");
        for (name, text) in [("Fmt", &def.fmt), ("Repr", &def.repr)] {
            if let Some(text) = text {
                let f = name.to_lowercase();
                let d = &def.name;
                out.push_str(&format!(
                    "impl {name}[{d}] {{\n  fn {f}(value: {d}) -> str ! alloc {{ {} }}\n}}\n\n",
                    literal(text)
                ));
            }
        }
    }
    out.push_str("fn shown[T: Fmt](x: T) -> str ! alloc { \"{x}\" }\n\n");
    out.push_str("fn written[T: Repr](x: T) -> str ! alloc { repr(x) }\n\n");
}

fn printed(m: &Model, v: &Val, way: Print) -> Vec<u8> {
    match way {
        Print::Fmt | Print::Shown => fmt(&m.defs, v),
        Print::Repr | Print::ReprInBraces | Print::Written => repr(&m.defs, v),
    }
}

fn print_line(way: Print, name: &str) -> String {
    match way {
        Print::Fmt => format!("  print(\"{{{name}}}\")\n"),
        Print::Repr => format!("  print(repr({name}))\n"),
        Print::ReprInBraces => format!("  print(\"{{repr({name})}}\")\n"),
        Print::Shown => format!("  print(shown({name}))\n"),
        Print::Written => format!("  print(written({name}))\n"),
    }
}

/// The program with a hole for each value, and what it prints.
fn write(m: &Model) -> (Written, Vec<u8>) {
    let mut template = String::from(HEADER);
    write_defs(m, &mut template);
    template.push_str("fn main() ! console, alloc {\n");
    let mut stdout = Vec::new();
    let mut values = Vec::new();
    let mut lines = Vec::new();
    for (i, (t, v, ways)) in m.values.iter().enumerate() {
        lines.push(ways.len());
        let name = format!("v{i}");
        template.push_str(&format!("  {name}: {} = {HOLE}\n", t.written(&m.defs)));
        values.push(expr(&m.defs, v));
        for &way in ways {
            template.push_str(&print_line(way, &name));
            stdout.extend(printed(m, v, way));
            stdout.push(b'\n');
        }
    }
    template.push_str("}\n");
    let written = Written {
        template,
        values,
        lines,
    };
    (written, stdout)
}

/// A valid program that prints values with the `Fmt` and `Repr` their
/// types are given or implement.
pub fn program(g: &mut Gen) -> ReprCase {
    let m = model(g, true, PRINTS);
    let (written, stdout) = write(&m);
    ReprCase {
        source: written.with(&written.values),
        stdout,
        mistake: None,
    }
}

/// `program`, where a type may be a `List` too.
pub fn program_with_lists(g: &mut Gen) -> ReprCase {
    let _lists = WithLists::new();
    program(g)
}

/// `program_with_mistake`, where a type may be a `List` too.
pub fn program_with_lists_and_mistake(g: &mut Gen) -> ReprCase {
    let _lists = WithLists::new();
    program_with_mistake(g)
}

/// A valid program in which no type implements `Fmt` or `Repr` itself, and
/// which prints the `Repr` of each value it binds, first, and then the
/// value in other ways, each on one line. Each value can be put in its
/// place as text.
pub fn program_to_rewrite(g: &mut Gen) -> Written {
    let mut m = model(g, false, PRINTS);
    for (t, _, ways) in &mut m.values {
        // The `Fmt` of a `str` is its text, which may break a line.
        if *t == Ty::Str {
            ways.retain(|w| !matches!(w, Print::Fmt | Print::Shown));
        }
        ways.insert(0, Print::Repr);
    }
    write(&m).0
}

/// The mistake to place.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Mistake {
    /// `without` takes the trait, `Fmt` or `Repr`, away from `Hidden`, and
    /// `main` prints, in the way given, a value of a type that holds it.
    Taken(&'static str, Print),
    /// `without Fmt for` a type there is not.
    NoType,
    /// `without Nope for` a type.
    NoTrait,
    /// `without Fmt for str`.
    Text,
    /// The same `without` twice.
    Twice,
    /// `without` a trait a type implements itself.
    Itself,
}

/// A type that holds `Hidden`, at any depth, among the types the program
/// defines.
fn holding(g: &mut Gen, defs: &[Def], depth: usize) -> Ty {
    let generic: Vec<usize> = (0..defs.len()).filter(|&d| defs[d].params > 0).collect();
    let inner = |g: &mut Gen| {
        if depth == 0 {
            Ty::Hidden
        } else {
            holding(g, defs, depth - 1)
        }
    };
    match g.below(if lists() { 5 } else { 4 }) {
        0 => Ty::Hidden,
        4 => Ty::List(Box::new(inner(g))),
        1 => Ty::Option(Box::new(inner(g))),
        2 => {
            let mut items: Vec<Ty> = (0..1 + g.below(2)).map(|_| ty(g, defs, 0, 1)).collect();
            let at = g.below(items.len() + 1);
            items.insert(at, inner(g));
            Ty::Tuple(items)
        }
        _ if !generic.is_empty() => {
            let d = *g.pick(&generic);
            let mut args: Vec<Ty> = (0..defs[d].params).map(|_| ty(g, defs, 0, 1)).collect();
            let at = g.below(args.len());
            args[at] = inner(g);
            Ty::Def(d, args)
        }
        _ => Ty::Hidden,
    }
}

/// A program with one mistake, about the `Fmt` and `Repr` a type is given.
pub fn program_with_mistake(g: &mut Gen) -> ReprCase {
    let m = model(g, true, PRINTS);
    let mistake = match g.below(7) {
        0 | 1 => Mistake::Taken("Fmt", *g.pick(&[Print::Fmt, Print::Shown])),
        2 | 3 => Mistake::Taken(
            "Repr",
            *g.pick(&[Print::Fmt, Print::Repr, Print::ReprInBraces, Print::Written]),
        ),
        4 => *g.pick(&[Mistake::NoType, Mistake::NoTrait, Mistake::Text]),
        _ => *g.pick(&[Mistake::Twice, Mistake::Itself]),
    };
    let mut source = String::from(HEADER);
    write_defs(&m, &mut source);
    source.push_str("struct Hidden {\n  key: str\n}\n\n");
    if mistake == Mistake::Itself {
        source.push_str(
            "impl Fmt[Hidden] {\n  fn fmt(value: Hidden) -> str ! alloc { \"a\" }\n}\n\n",
        );
    }
    let without = |trait_name: &str| format!("without {trait_name} for ");
    let placed = match mistake {
        Mistake::Taken(name, _) => {
            source.push_str(&format!("{}Hidden\n\n", without(name)));
            None
        }
        Mistake::NoType => {
            source.push_str(&without("Fmt"));
            let at = pos_after(&source);
            source.push_str("Nope\n\n");
            Some((at, "there is no type named `Nope`".to_string()))
        }
        Mistake::NoTrait => {
            source.push_str("without ");
            let at = pos_after(&source);
            source.push_str("Nope for Hidden\n\n");
            Some((at, "there is no trait named `Nope`".to_string()))
        }
        Mistake::Text => {
            source.push_str(&without("Fmt"));
            let at = pos_after(&source);
            source.push_str("str\n\n");
            Some((at, "`str` is neither".to_string()))
        }
        Mistake::Twice => {
            let name = *g.pick(&["Fmt", "Repr"]);
            source.push_str(&format!("{}Hidden\n", without(name)));
            let at = pos_after(&source);
            source.push_str(&format!("{}Hidden\n\n", without(name)));
            Some((at, format!("`{name}` is already taken away from `Hidden`")))
        }
        Mistake::Itself => {
            let at = pos_after(&source);
            source.push_str(&format!("{}Hidden\n\n", without("Fmt")));
            Some((at, "`Hidden` implements `Fmt` itself".to_string()))
        }
    };
    source.push_str("fn main() ! console, alloc {\n");
    let at = g.below(m.values.len() + 1);
    let mut expected = placed.map(|(at, mentions)| Expected {
        at,
        mentions,
        fix: None,
    });
    for i in 0..=m.values.len() {
        if let (Mistake::Taken(name, way), true) = (mistake, i == at) {
            let t = holding(g, &m.defs, 2);
            let v = value(g, &m.defs, &t);
            source.push_str(&format!(
                "  hidden: {} = {}\n",
                t.written(&m.defs),
                expr(&m.defs, &v)
            ));
            let line = print_line(way, "hidden");
            let offset = match way {
                Print::Fmt | Print::ReprInBraces => line.find('{').expect("a brace") + 1,
                _ => line.find("(").expect("a call") + 1,
            };
            let place = pos_after(&format!("{source}{}", &line[..offset]));
            source.push_str(&line);
            // Each type that holds `Hidden` is given `Fmt` only if
            // `Hidden` has it, and `Repr` likewise, and `Fmt` is given only
            // where `Repr` is, so the error names `Hidden` and the trait
            // taken from it, unless `Fmt` is taken from `Hidden` itself.
            let wanted = match way {
                Print::Fmt | Print::Shown => "Fmt",
                _ => "Repr",
            };
            let mentions = if t == Ty::Hidden && name == wanted {
                format!("`{wanted}` for `Hidden`")
            } else {
                format!("as `Hidden` has no `{name}`")
            };
            expected = Some(Expected {
                at: place,
                mentions,
                fix: None,
            });
        }
        if let Some((t, v, ways)) = m.values.get(i) {
            let name = format!("v{i}");
            let line = format!("  {name}: {} = {}\n", t.written(&m.defs), expr(&m.defs, v));
            source.push_str(&line);
            for &way in ways {
                source.push_str(&print_line(way, &name));
            }
        }
    }
    source.push_str("}\n");
    ReprCase {
        source,
        stdout: Vec::new(),
        mistake: expected,
    }
}
