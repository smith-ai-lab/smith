//! The generator of programs that compare values with `==` and `!=` and
//! choose with `if`, `not`, `and`, and `or`, which can also place one
//! mistake in what it builds.
//!
//! A program defines a few structs and enums, each of which may have type
//! parameters, and whose fields and cases hold text, a `bool`, `()`, a type
//! parameter, an `Option`, a tuple, or a struct or an enum defined before
//! it. A struct or an enum without type parameters may implement `Eq`
//! itself. `main` binds values of such types, with any nesting and type
//! arguments, and compares them, directly and through a function whose
//! bound is `Eq`. It combines what the comparisons give with `not`, `and`,
//! `or`, and `if` in every grouping the precedence table allows, and with
//! calls that print as they give a `bool`, so the output shows which parts
//! run. It chooses with `if`, with and without `else`, as a statement and as
//! an expression, and with `match` on a `bool`, inside lambdas and the
//! blocks of a `match`.
//!
//! The generator works out what the program prints from the values it
//! built, as `knowledge/language/comparisons.md` and
//! `knowledge/language/if.md` say they compare and choose.

use crate::generate::Gen;
use crate::programs::{Expected, PIECES, pos_after};

/// A program that compares and chooses: its source, what it prints, and
/// the mistake placed in it, if any.
pub struct ChoiceCase {
    pub source: String,
    pub stdout: Vec<u8>,
    pub mistake: Option<Expected>,
}

impl std::fmt::Debug for ChoiceCase {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self.source)
    }
}

const NAMES: &[&str] = &["Cat", "Pet", "Bag"];
const FIELDS: &[&str] = &["name", "left", "right"];
const CASES: &[&str] = &["cat", "dog", "none"];
const PARAMS: &[&str] = &["T", "U"];
/// The texts a call that prints writes, as a `str` literal holds them.
const LABELS: &[&str] = &["a", "b", "c", "d"];

/// How deep a type of a field, a case, or a value is built.
const DEPTH: usize = 2;
/// How deep a condition is built.
const CONDITION: usize = 3;
/// How deep statements are nested in blocks.
const NESTING: usize = 2;

#[derive(Clone, Debug, PartialEq, Eq)]
enum Ty {
    Str,
    Bool,
    Unit,
    Param(usize),
    /// A struct or an enum the program defines, with its type arguments.
    Def(usize, Vec<Ty>),
    Option(Box<Ty>),
    Tuple(Vec<Ty>),
    /// `Hidden`, a struct from which a mistake takes `Eq` away.
    Hidden,
}

impl Ty {
    fn subst(&self, args: &[Ty]) -> Ty {
        match self {
            Ty::Param(p) => args[*p].clone(),
            Ty::Def(d, of) => Ty::Def(*d, of.iter().map(|t| t.subst(args)).collect()),
            Ty::Option(t) => Ty::Option(Box::new(t.subst(args))),
            Ty::Tuple(of) => Ty::Tuple(of.iter().map(|t| t.subst(args)).collect()),
            t => t.clone(),
        }
    }

    fn mentions(&self, p: usize) -> bool {
        match self {
            Ty::Param(q) => *q == p,
            Ty::Def(_, of) | Ty::Tuple(of) => of.iter().any(|t| t.mentions(p)),
            Ty::Option(t) => t.mentions(p),
            Ty::Str | Ty::Bool | Ty::Unit | Ty::Hidden => false,
        }
    }

    fn written(&self, defs: &[Def]) -> String {
        let list = |of: &[Ty]| -> String {
            let items: Vec<String> = of.iter().map(|t| t.written(defs)).collect();
            items.join(", ")
        };
        match self {
            Ty::Str => "str".to_string(),
            Ty::Bool => "bool".to_string(),
            Ty::Unit => "()".to_string(),
            Ty::Param(p) => PARAMS[*p].to_string(),
            Ty::Def(d, of) if of.is_empty() => defs[*d].name.clone(),
            Ty::Def(d, of) => format!("{}[{}]", defs[*d].name, list(of)),
            Ty::Option(t) => format!("Option[{}]", t.written(defs)),
            Ty::Tuple(of) => format!("({})", list(of)),
            Ty::Hidden => "Hidden".to_string(),
        }
    }

    /// What a value of the type is, as an error describes it.
    fn described(&self, defs: &[Def]) -> String {
        match self {
            Ty::Str => "text".to_string(),
            Ty::Unit => "nothing".to_string(),
            t => format!("a `{}`", t.written(defs)),
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
}

#[derive(Clone, Debug)]
enum Val {
    /// The pieces of a literal.
    Str(Vec<usize>),
    Bool(bool),
    Unit,
    /// A value of a struct, or a case of an enum with what it holds.
    Def(usize, Option<usize>, Vec<Val>),
    Some(Box<Val>),
    None,
    Tuple(Vec<Val>),
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

/// A type among `params` type parameters, that names only the definitions
/// in `defs`.
fn ty(g: &mut Gen, defs: &[Def], params: usize, depth: usize) -> Ty {
    let mut choices = vec![0, 1, 2];
    if params > 0 {
        choices.push(3);
    }
    if depth > 0 {
        choices.extend([4, 5]);
        if !defs.is_empty() {
            choices.extend([6, 6]);
        }
    }
    match *g.pick(&choices) {
        0 => Ty::Str,
        1 => Ty::Bool,
        2 => Ty::Unit,
        3 => Ty::Param(g.below(params)),
        4 => Ty::Option(Box::new(ty(g, defs, params, depth - 1))),
        5 => Ty::Tuple(
            (0..2 + g.below(2))
                .map(|_| ty(g, defs, params, depth - 1))
                .collect(),
        ),
        _ => {
            let d = g.below(defs.len());
            let args = (0..defs[d].params)
                .map(|_| ty(g, defs, params, depth - 1))
                .collect();
            Ty::Def(d, args)
        }
    }
}

fn definitions(g: &mut Gen) -> Vec<Def> {
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
                items.insert(at, (format!("of{p}"), Some(Ty::Param(p))));
            }
        }
        defs.push(Def {
            name: name.to_string(),
            is_enum,
            params,
            items,
        });
    }
    defs
}

/// A type with no type parameter, for a value to bind.
fn concrete(g: &mut Gen, defs: &[Def]) -> Ty {
    if g.one_in(3) {
        return ty(g, defs, 0, DEPTH);
    }
    let d = g.below(defs.len());
    let args = (0..defs[d].params).map(|_| ty(g, defs, 0, 1)).collect();
    Ty::Def(d, args)
}

fn value(g: &mut Gen, defs: &[Def], t: &Ty) -> Val {
    match t {
        Ty::Str => Val::Str(pieces(g)),
        Ty::Bool => Val::Bool(g.one_in(2)),
        Ty::Unit => Val::Unit,
        Ty::Option(inner) if g.one_in(2) => Val::Some(Box::new(value(g, defs, inner))),
        Ty::Option(_) => Val::None,
        Ty::Tuple(of) => Val::Tuple(of.iter().map(|t| value(g, defs, t)).collect()),
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

/// A value of the type that is often equal to `v`, and otherwise differs
/// from it in some part.
fn similar(g: &mut Gen, defs: &[Def], t: &Ty, v: &Val) -> Val {
    if g.one_in(4) {
        return value(g, defs, t);
    }
    match (t, v) {
        (Ty::Tuple(of), Val::Tuple(items)) => Val::Tuple(
            (of.iter().zip(items))
                .map(|(t, v)| similar(g, defs, t, v))
                .collect(),
        ),
        (Ty::Option(inner), Val::Some(v)) => Val::Some(Box::new(similar(g, defs, inner, v))),
        (Ty::Def(d, args), Val::Def(_, case, held)) => {
            let def = &defs[*d];
            let types: Vec<Ty> = match case {
                Some(c) => def.items[*c].1.iter().map(|t| t.subst(args)).collect(),
                None => (def.items.iter())
                    .map(|(_, t)| t.as_ref().expect("a field").subst(args))
                    .collect(),
            };
            let held = (types.iter().zip(held))
                .map(|(t, v)| similar(g, defs, t, v))
                .collect();
            Val::Def(*d, *case, held)
        }
        _ => v.clone(),
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
        Val::Bool(b) => b.to_string(),
        Val::Unit => "()".to_string(),
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
        Val::Hidden => "Hidden(key: \"k\")".to_string(),
    }
}

/// Whether two values of one type are equal, as the `Eq` each type is
/// given says.
fn equal(a: &Val, b: &Val) -> bool {
    let all = |x: &[Val], y: &[Val]| x.iter().zip(y).all(|(x, y)| equal(x, y));
    match (a, b) {
        (Val::Str(x), Val::Str(y)) => text_of(x) == text_of(y),
        (Val::Bool(x), Val::Bool(y)) => x == y,
        (Val::Unit, Val::Unit) | (Val::None, Val::None) => true,
        (Val::Def(_, c, x), Val::Def(_, k, y)) => c == k && all(x, y),
        (Val::Some(x), Val::Some(y)) => equal(x, y),
        (Val::Tuple(x), Val::Tuple(y)) => all(x, y),
        _ => false,
    }
}

/// A condition: an expression that gives a `bool`.
#[derive(Clone, Debug)]
enum Cond {
    Lit(bool),
    /// A `bool` bound to `b{i}`.
    Name(usize),
    /// `v{left} == right` or `v{left} != right`, where `right` is another
    /// value bound to a name of the same type or an expression.
    Compare(usize, Right, bool),
    /// `same(v{left}, v{right})`, whose bound is `Eq`.
    Through(usize, usize),
    /// Two conditions compared, each in parentheses unless it stands
    /// alone.
    Both(Box<Cond>, Box<Cond>, bool),
    /// `loud("label", c)`, which prints the label once `c` is worked out.
    Loud(usize, Box<Cond>),
    Not(Box<Cond>),
    And(Box<Cond>, Box<Cond>),
    Or(Box<Cond>, Box<Cond>),
    If(Box<Cond>, Box<Cond>, Box<Cond>),
    Paren(Box<Cond>),
}

#[derive(Clone, Debug)]
enum Right {
    Name(usize),
    Expr(Val),
}

/// A statement of `main`, or of a block in it.
#[derive(Clone, Debug)]
enum Stmt {
    /// `print("{c}")`.
    Print(Cond),
    /// `print(repr(c))`.
    Repr(Cond),
    /// `print(word(c))`, where `word` chooses its text with `if`.
    Word(Cond),
    /// `b{i} = c`, at the top of `main` only.
    Bind(usize, Cond),
    /// `if c { ... }`, with `else` and its block if it has one. A block
    /// that holds one `if` alone is written `else if`.
    If(Cond, Vec<Stmt>, Option<Vec<Stmt>>),
    /// `x{i} = if c { "a" } else { "b" }`, then `print(x{i})`.
    Choose(usize, Cond, usize, usize),
    /// `x{i} = if c { "a" }`, which gives `()`, then `print("{x{i}}")`,
    /// or `print(repr(x{i}))` if it says so.
    Unit(usize, Cond, usize, bool),
    /// `var x{i} = "a"`, `if c { x{i} = "b" }`, then `print(x{i})`.
    Assign(usize, Cond, usize, usize),
    /// `match c { true { ... } false { ... } }`, with each case written
    /// with a leading dot if it says so.
    Match(Cond, Vec<Stmt>, Vec<Stmt>, bool),
    /// `twice { ... }`, which runs the lambda's body twice.
    Twice(Vec<Stmt>),
}

struct Model {
    defs: Vec<Def>,
    /// The values bound to `v{i}`, with their types.
    values: Vec<(Ty, Val)>,
    main: Vec<Stmt>,
}

/// What generating a condition or a statement can name.
struct Scope<'m> {
    defs: &'m [Def],
    values: &'m [(Ty, Val)],
    /// How many `bool`s are bound to `b{i}` so far.
    bools: usize,
    /// The next number for a name of a statement's own.
    next: usize,
}

fn cond(g: &mut Gen, s: &Scope, depth: usize) -> Cond {
    let leaf = |g: &mut Gen| -> Cond {
        let mut choices = vec![0, 1, 1, 2];
        if s.bools > 0 {
            choices.push(3);
        }
        match *g.pick(&choices) {
            0 => Cond::Lit(g.one_in(2)),
            1 => {
                let left = g.below(s.values.len());
                let (t, v) = &s.values[left];
                let same: Vec<usize> = (0..s.values.len())
                    .filter(|&j| s.values[j].0 == *t)
                    .collect();
                let right = if g.one_in(2) {
                    Right::Name(*g.pick(&same))
                } else {
                    Right::Expr(similar(g, s.defs, t, v))
                };
                Cond::Compare(left, right, g.one_in(3))
            }
            2 => {
                let left = g.below(s.values.len());
                let same: Vec<usize> = (0..s.values.len())
                    .filter(|&j| s.values[j].0 == s.values[left].0)
                    .collect();
                Cond::Through(left, *g.pick(&same))
            }
            _ => Cond::Name(g.below(s.bools)),
        }
    };
    if depth == 0 {
        return leaf(g);
    }
    let inner = |g: &mut Gen| Box::new(cond(g, s, depth - 1));
    match g.below(10) {
        0 | 1 => leaf(g),
        2 => Cond::Loud(g.below(LABELS.len()), inner(g)),
        3 => Cond::Not(inner(g)),
        4 => Cond::And(inner(g), inner(g)),
        5 => Cond::Or(inner(g), inner(g)),
        6 => Cond::If(inner(g), inner(g), inner(g)),
        7 => Cond::Both(inner(g), inner(g), g.one_in(2)),
        8 => Cond::Paren(inner(g)),
        _ => Cond::Loud(g.below(LABELS.len()), Box::new(leaf(g))),
    }
}

/// How tightly each kind of condition binds, as the precedence table
/// orders `or`, `and`, `not`, and the comparisons. An `if` binds least.
fn level(c: &Cond) -> usize {
    match c {
        Cond::If(..) => 0,
        Cond::Or(..) => 1,
        Cond::And(..) => 2,
        Cond::Not(_) => 3,
        Cond::Compare(..) | Cond::Both(..) => 4,
        _ => 5,
    }
}

/// The condition as it is written where it must bind at least as tightly
/// as `need`, in parentheses if it does not.
fn written(m: &Model, c: &Cond, need: usize) -> String {
    let text = match c {
        Cond::Lit(b) => b.to_string(),
        Cond::Name(i) => format!("b{i}"),
        Cond::Compare(left, right, differ) => {
            let op = if *differ { "!=" } else { "==" };
            let right = match right {
                Right::Name(j) => format!("v{j}"),
                Right::Expr(v) => expr(&m.defs, v),
            };
            format!("v{left} {op} {right}")
        }
        Cond::Through(a, b) => format!("same(v{a}, v{b})"),
        Cond::Both(a, b, differ) => {
            let op = if *differ { "!=" } else { "==" };
            format!("{} {op} {}", written(m, a, 5), written(m, b, 5))
        }
        Cond::Loud(label, c) => format!("loud(\"{}\", {})", LABELS[*label], written(m, c, 0)),
        Cond::Not(c) => format!("not {}", written(m, c, 3)),
        Cond::And(a, b) => format!("{} and {}", written(m, a, 2), written(m, b, 3)),
        Cond::Or(a, b) => format!("{} or {}", written(m, a, 1), written(m, b, 2)),
        Cond::If(c, a, b) => format!(
            "if {} {{ {} }} else {{ {} }}",
            written(m, c, 0),
            written(m, a, 0),
            written(m, b, 0)
        ),
        Cond::Paren(c) => format!("({})", written(m, c, 0)),
    };
    if level(c) < need {
        format!("({text})")
    } else {
        text
    }
}

/// What the condition gives, with what it prints going to `out`.
fn truth(m: &Model, bools: &[bool], c: &Cond, out: &mut Vec<u8>) -> bool {
    match c {
        Cond::Lit(b) => *b,
        Cond::Name(i) => bools[*i],
        Cond::Compare(left, right, differ) => {
            let right = match right {
                Right::Name(j) => &m.values[*j].1,
                Right::Expr(v) => v,
            };
            equal(&m.values[*left].1, right) != *differ
        }
        Cond::Through(a, b) => equal(&m.values[*a].1, &m.values[*b].1),
        Cond::Both(a, b, differ) => {
            let a = truth(m, bools, a, out);
            let b = truth(m, bools, b, out);
            (a == b) != *differ
        }
        Cond::Loud(label, c) => {
            let given = truth(m, bools, c, out);
            out.extend(LABELS[*label].bytes());
            out.push(b'\n');
            given
        }
        Cond::Not(c) => !truth(m, bools, c, out),
        Cond::And(a, b) => truth(m, bools, a, out) && truth(m, bools, b, out),
        Cond::Or(a, b) => truth(m, bools, a, out) || truth(m, bools, b, out),
        Cond::If(c, a, b) => {
            if truth(m, bools, c, out) {
                truth(m, bools, a, out)
            } else {
                truth(m, bools, b, out)
            }
        }
        Cond::Paren(c) => truth(m, bools, c, out),
    }
}

fn block(g: &mut Gen, s: &mut Scope, nesting: usize) -> Vec<Stmt> {
    (0..g.below(3))
        .map(|_| stmt(g, s, nesting, false))
        .collect()
}

fn stmt(g: &mut Gen, s: &mut Scope, nesting: usize, top: bool) -> Stmt {
    let c = |g: &mut Gen, s: &Scope| {
        let depth = g.below(CONDITION + 1);
        cond(g, s, depth)
    };
    let mut choices = vec![0, 1, 2, 4, 5, 6];
    if top {
        choices.extend([3, 3]);
    }
    if nesting > 0 {
        choices.extend([7, 7, 8, 9]);
    }
    let label = |g: &mut Gen| g.below(LABELS.len());
    let fresh = |s: &mut Scope| {
        s.next += 1;
        s.next - 1
    };
    match *g.pick(&choices) {
        0 => Stmt::Print(c(g, s)),
        1 => Stmt::Repr(c(g, s)),
        2 => Stmt::Word(c(g, s)),
        3 => {
            let given = c(g, s);
            s.bools += 1;
            Stmt::Bind(s.bools - 1, given)
        }
        4 => Stmt::Choose(fresh(s), c(g, s), label(g), label(g)),
        5 => Stmt::Unit(fresh(s), c(g, s), label(g), g.one_in(2)),
        6 => Stmt::Assign(fresh(s), c(g, s), label(g), label(g)),
        7 => {
            let given = c(g, s);
            let then = block(g, s, nesting - 1);
            let otherwise = if g.one_in(3) {
                None
            } else if g.one_in(2) {
                let given = c(g, s);
                let then = block(g, s, nesting - 1);
                let rest = (!g.one_in(3)).then(|| block(g, s, nesting - 1));
                Some(vec![Stmt::If(given, then, rest)])
            } else {
                Some(block(g, s, nesting - 1))
            };
            Stmt::If(given, then, otherwise)
        }
        8 => {
            let given = c(g, s);
            let then = block(g, s, nesting - 1);
            let otherwise = block(g, s, nesting - 1);
            Stmt::Match(given, then, otherwise, g.one_in(2))
        }
        _ => Stmt::Twice(block(g, s, nesting - 1)),
    }
}

fn model(g: &mut Gen) -> Model {
    let defs = definitions(g);
    // A value is often bound next to one like it, so that two names often
    // hold equal values, and values that differ in one part.
    let mut values: Vec<(Ty, Val)> = Vec::new();
    for _ in 0..1 + g.below(3) {
        let t = concrete(g, &defs);
        let v = value(g, &defs, &t);
        if g.one_in(2) {
            let like = similar(g, &defs, &t, &v);
            values.push((t.clone(), like));
        }
        values.push((t, v));
    }
    let mut s = Scope {
        defs: &defs,
        values: &values,
        bools: 0,
        next: 0,
    };
    let main = (0..1 + g.below(5))
        .map(|_| stmt(g, &mut s, NESTING, true))
        .collect();
    Model { defs, values, main }
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
    }
    out.push_str("fn same[T: Eq](a: T, b: T) -> bool { a == b }\n\n");
    out.push_str(
        "fn loud(text: str, given: bool) -> bool ! console {\n  print(text)\n  given\n}\n\n",
    );
    out.push_str("fn word(given: bool) -> str {\n  if given { \"yes\" } else { \"no\" }\n}\n\n");
    out.push_str("fn twice(body: () -> ()) {\n  body()\n  body()\n}\n\n");
}

fn indent(depth: usize) -> String {
    "  ".repeat(depth)
}

fn write_block(m: &Model, stmts: &[Stmt], depth: usize, out: &mut String) {
    for s in stmts {
        write_stmt(m, s, depth, out);
    }
}

fn write_stmt(m: &Model, s: &Stmt, depth: usize, out: &mut String) {
    let at = indent(depth);
    let c = |c: &Cond| written(m, c, 0);
    match s {
        Stmt::Print(given) => out.push_str(&format!("{at}print(\"{{{}}}\")\n", c(given))),
        Stmt::Repr(given) => out.push_str(&format!("{at}print(repr({}))\n", c(given))),
        Stmt::Word(given) => out.push_str(&format!("{at}print(word({}))\n", c(given))),
        Stmt::Bind(i, given) => out.push_str(&format!("{at}b{i} = {}\n", c(given))),
        Stmt::If(given, then, otherwise) => {
            out.push_str(&format!("{at}if {} {{\n", c(given)));
            write_block(m, then, depth + 1, out);
            let mut otherwise = otherwise.as_ref();
            while let Some(stmts) = otherwise {
                match stmts.as_slice() {
                    [Stmt::If(given, then, rest)] => {
                        out.push_str(&format!("{at}}} else if {} {{\n", c(given)));
                        write_block(m, then, depth + 1, out);
                        otherwise = rest.as_ref();
                    }
                    stmts => {
                        out.push_str(&format!("{at}}} else {{\n"));
                        write_block(m, stmts, depth + 1, out);
                        otherwise = None;
                    }
                }
            }
            out.push_str(&format!("{at}}}\n"));
        }
        Stmt::Choose(i, given, a, b) => {
            out.push_str(&format!(
                "{at}x{i} = if {} {{ \"{}\" }} else {{ \"{}\" }}\n{at}print(x{i})\n",
                c(given),
                LABELS[*a],
                LABELS[*b]
            ));
        }
        Stmt::Unit(i, given, a, repr) => {
            let shown = if *repr {
                format!("repr(x{i})")
            } else {
                format!("\"{{x{i}}}\"")
            };
            out.push_str(&format!(
                "{at}x{i} = if {} {{ \"{}\" }}\n{at}print({shown})\n",
                c(given),
                LABELS[*a]
            ));
        }
        Stmt::Assign(i, given, a, b) => {
            out.push_str(&format!(
                "{at}var x{i} = \"{}\"\n{at}if {} {{ x{i} = \"{}\" }}\n{at}print(x{i})\n",
                LABELS[*a],
                c(given),
                LABELS[*b]
            ));
        }
        Stmt::Match(given, then, otherwise, dot) => {
            let dot = if *dot { "." } else { "" };
            out.push_str(&format!("{at}match {} {{\n", c(given)));
            out.push_str(&format!("{at}  {dot}true {{\n"));
            write_block(m, then, depth + 2, out);
            out.push_str(&format!("{at}  }}\n{at}  {dot}false {{\n"));
            write_block(m, otherwise, depth + 2, out);
            out.push_str(&format!("{at}  }}\n{at}}}\n"));
        }
        Stmt::Twice(body) => {
            out.push_str(&format!("{at}twice {{\n"));
            write_block(m, body, depth + 1, out);
            out.push_str(&format!("{at}}}\n"));
        }
    }
}

/// Runs the statements as the program does, with what they print going to
/// `out`.
fn run(m: &Model, stmts: &[Stmt], bools: &mut Vec<bool>, out: &mut Vec<u8>) {
    let line = |out: &mut Vec<u8>, text: &str| {
        out.extend(text.bytes());
        out.push(b'\n');
    };
    for s in stmts {
        match s {
            Stmt::Print(c) | Stmt::Repr(c) => {
                let given = truth(m, bools, c, out);
                line(out, &given.to_string());
            }
            Stmt::Word(c) => {
                let given = truth(m, bools, c, out);
                line(out, if given { "yes" } else { "no" });
            }
            Stmt::Bind(_, c) => {
                let given = truth(m, bools, c, out);
                bools.push(given);
            }
            Stmt::If(c, then, otherwise) => {
                if truth(m, bools, c, out) {
                    run(m, then, bools, out);
                } else if let Some(otherwise) = otherwise {
                    run(m, otherwise, bools, out);
                }
            }
            Stmt::Choose(_, c, a, b) => {
                let given = truth(m, bools, c, out);
                line(out, LABELS[if given { *a } else { *b }]);
            }
            Stmt::Unit(_, c, ..) => {
                truth(m, bools, c, out);
                line(out, "()");
            }
            Stmt::Assign(_, c, a, b) => {
                let given = truth(m, bools, c, out);
                line(out, LABELS[if given { *b } else { *a }]);
            }
            Stmt::Match(c, then, otherwise, _) => {
                if truth(m, bools, c, out) {
                    run(m, then, bools, out);
                } else {
                    run(m, otherwise, bools, out);
                }
            }
            Stmt::Twice(body) => {
                run(m, body, bools, out);
                run(m, body, bools, out);
            }
        }
    }
}

fn write_values(m: &Model, out: &mut String) {
    for (i, (t, v)) in m.values.iter().enumerate() {
        out.push_str(&format!(
            "  v{i}: {} = {}\n",
            t.written(&m.defs),
            expr(&m.defs, v)
        ));
    }
}

/// A valid program that compares values and chooses with what the
/// comparisons give.
pub fn program(g: &mut Gen) -> ChoiceCase {
    let m = model(g);
    let mut source = String::new();
    write_defs(&m, &mut source);
    source.push_str("fn main() ! console, alloc {\n");
    write_values(&m, &mut source);
    write_block(&m, &m.main, 1, &mut source);
    source.push_str("}\n");
    let mut stdout = Vec::new();
    run(&m, &m.main, &mut Vec::new(), &mut stdout);
    ChoiceCase {
        source,
        stdout,
        mistake: None,
    }
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
    match g.below(4) {
        0 => Ty::Hidden,
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

/// A program with one mistake about comparing or choosing, in a statement
/// of `main` after its values are bound.
pub fn program_with_mistake(g: &mut Gen) -> ChoiceCase {
    let m = model(g);
    let mut source = String::new();
    write_defs(&m, &mut source);
    source.push_str("struct Hidden {\n  key: str\n}\n\nwithout Eq for Hidden\n\n");
    source.push_str("fn risky() -> Result[str, str] { .ok(\"a\") }\n\n");
    source.push_str("fn main() ! console, alloc {\n");
    write_values(&m, &mut source);
    let at = g.below(m.main.len() + 1);
    let mut before = String::new();
    write_block(&m, &m.main[..at], 1, &mut before);
    source.push_str(&before);
    let s = Scope {
        defs: &m.defs,
        values: &m.values,
        bools: (m.main[..at].iter())
            .filter(|s| matches!(s, Stmt::Bind(..)))
            .count(),
        next: 0,
    };
    // A value that is not a `bool`, written as an expression.
    let not_bool = |g: &mut Gen| -> (String, Ty) {
        let others: Vec<usize> = (0..m.values.len())
            .filter(|&i| m.values[i].0 != Ty::Bool)
            .collect();
        if others.is_empty() || g.one_in(3) {
            let t = Ty::Str;
            let v = value(g, &m.defs, &t);
            (expr(&m.defs, &v), t)
        } else {
            let i = *g.pick(&others);
            (format!("v{i}"), m.values[i].0.clone())
        }
    };
    let ok = |g: &mut Gen| written(&m, &cond(g, &s, 1), 5);
    // The statement, as the text before the mistake and the rest, what the
    // error must mention, and what its fix must mention, if it has one.
    let word = *g.pick(&["true", "false"]);
    let named = format!("`{word}` is a case of `bool`");
    // A definition named `true` or `false` after `main`, as the text before
    // the name and the rest, if the mistake is one.
    let mut item = None;
    let (lead, rest, mentions, fix) = match g.below(8) {
        6 => {
            let (lead, rest) = *g.pick(&[
                ("fn ", "() {}"),
                ("fn spare(", ": str) {}"),
                ("fn spare[", "]() {}"),
                ("struct ", " {\n  a: str\n}"),
                ("struct Spare {\n  ", ": str\n}"),
                ("enum ", " {\n  a\n}"),
                ("enum Spare {\n  a\n  ", "\n}"),
                ("trait ", "[T] {\n  fn spare_of(x: T)\n}"),
            ]);
            item = Some((lead.to_string(), format!("{word}{rest}")));
            (String::new(), String::new(), named, None)
        }
        7 => {
            let (lead, rest) = *g.pick(&[
                ("", " = \"a\""),
                ("", ": str = \"a\""),
                ("var ", " = \"a\""),
                ("(var ", ", spare) = (\"a\", \"b\")"),
                ("twice fn(", ") { }"),
            ]);
            (lead.to_string(), format!("{word}{rest}"), named, None)
        }
        0 => {
            let (value, _) = not_bool(g);
            let (lead, rest) = match g.below(5) {
                0 => ("if ".to_string(), format!("{value} {{ print(\"a\") }}")),
                1 => ("w = not ".to_string(), value),
                2 => ("w = ".to_string(), format!("{value} and {}", ok(g))),
                3 => (format!("w = {} or ", ok(g)), value),
                _ => (
                    "w = if true { true } else { ".to_string(),
                    format!("{value} }}"),
                ),
            };
            let mentions = if lead.starts_with("w = if") {
                "the blocks of an `if`".to_string()
            } else {
                "where a `bool` is needed".to_string()
            };
            (lead, rest, mentions, None)
        }
        1 => {
            let (value, _) = not_bool(g);
            let first = ok(g);
            let lead = format!("w = if {} {{ {first} }} else {{ ", ok(g));
            let mentions = "the blocks of an `if`".to_string();
            (lead, format!("{value} }}"), mentions, None)
        }
        2 => {
            let left = g.below(m.values.len());
            let t = &m.values[left].0;
            let mut others = vec![Ty::Str, Ty::Bool, Ty::Unit];
            others.push(concrete(g, &m.defs));
            others.retain(|o| o != t);
            let other = g.pick(&others).clone();
            let v = value(g, &m.defs, &other);
            source.push_str(&format!(
                "  other: {} = {}\n",
                other.written(&m.defs),
                expr(&m.defs, &v)
            ));
            let op = *g.pick(&["==", "!="]);
            (
                format!("w = v{left} {op} "),
                "other".to_string(),
                format!("where {} is needed", t.described(&m.defs)),
                None,
            )
        }
        3 => {
            let t = holding(g, &m.defs, 2);
            let v = value(g, &m.defs, &t);
            source.push_str(&format!(
                "  hidden: {} = {}\n",
                t.written(&m.defs),
                expr(&m.defs, &v)
            ));
            let mentions = if t == Ty::Hidden {
                "`Eq` for `Hidden`".to_string()
            } else {
                "as `Hidden` has no `Eq`".to_string()
            };
            let rest = match g.below(3) {
                0 => "hidden == hidden".to_string(),
                1 => "hidden != hidden".to_string(),
                _ => "same(hidden, hidden)".to_string(),
            };
            ("w = ".to_string(), rest, mentions, None)
        }
        4 => {
            let left = g.below(m.values.len());
            let ops = ["==", "!="];
            let lead = format!("w = v{left} {} v{left} ", g.pick(&ops));
            let rest = format!("{} v{left}", g.pick(&ops));
            let fix = Some("parentheses".to_string());
            (lead, rest, "parentheses".to_string(), fix)
        }
        _ => (
            format!("if {} {{ ", ok(g)),
            "risky() }".to_string(),
            "dropped".to_string(),
            Some("write `try` before it".to_string()),
        ),
    };
    let mut place = None;
    if item.is_none() {
        let line = format!("  {lead}");
        place = Some(pos_after(&format!("{source}{line}")));
        source.push_str(&format!("{line}{rest}\n"));
    }
    let mut after = String::new();
    write_block(&m, &m.main[at..], 1, &mut after);
    source.push_str(&after);
    source.push_str("}\n");
    if let Some((lead, rest)) = item {
        source.push_str(&format!("\n{lead}"));
        place = Some(pos_after(&source));
        source.push_str(&format!("{rest}\n"));
    }
    let place = place.expect("a place");
    ChoiceCase {
        source,
        stdout: Vec::new(),
        mistake: Some(Expected {
            at: place,
            mentions,
            fix,
        }),
    }
}
