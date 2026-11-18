//! The generator of programs whose strings hold expressions, which can also
//! place one mistake in what it builds.
//!
//! A program has a struct `Cat` and an enum `Pet`, each of which implements
//! `Fmt` with strings that hold expressions, a tuple type `(str, Cat)`,
//! whose `Fmt` is given and writes the `Repr` that `Cat` is given, and
//! three functions it always defines: `double`, which joins a text to
//! itself, `apply`, which puts what a function makes of a text in a string,
//! and `show`, which puts a value of any type that implements `Fmt` in a
//! string. A few more functions take and return text, a `Cat`, a `Pet`, or a
//! tuple, bind names, take tuples apart, take a `Pet` apart with `match`, and
//! call the functions before them. Every string may hold expressions of
//! every type that implements `Fmt`, strings among them, and a lambda passed
//! to `apply` builds text too. `main` does the same, and prints. It may print
//! a text built by doubling one many times, far larger than a page of
//! memory.
//!
//! The generator first builds a model of the program and works out from it
//! what the program prints. Then it writes the model out, placing the
//! mistake as it writes.

use crate::generate::Gen;
use crate::programs::{Expected, PIECES, pos_after};

use smith::diag::Pos;

/// A program whose strings hold expressions: its source, what it prints,
/// and the mistake placed in it, if any.
pub struct TextCase {
    pub source: String,
    pub stdout: Vec<u8>,
    pub mistake: Option<Expected>,
}

impl std::fmt::Debug for TextCase {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self.source)
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Ty {
    Str,
    Cat,
    Pet,
    /// `(str, Cat)`.
    Pair,
}

const TYPES: &[Ty] = &[Ty::Str, Ty::Cat, Ty::Pet, Ty::Pair];

impl Ty {
    fn written(self) -> &'static str {
        match self {
            Ty::Str => "str",
            Ty::Cat => "Cat",
            Ty::Pet => "Pet",
            Ty::Pair => "(str, Cat)",
        }
    }
}

#[derive(Clone, Debug)]
enum Expr {
    /// A literal, as the places of its pieces in `PIECES`.
    Lit(Vec<usize>),
    /// A string that holds at least one expression.
    Interp(Vec<Part>),
    Name(String),
    /// `Cat(name: e)`.
    Cat(Box<Expr>),
    /// `e.name`, of a `Cat`.
    CatName(Box<Expr>),
    /// `Pet.cat(e)`.
    PetCat(Box<Expr>),
    /// `Pet.none`.
    PetNone,
    Pair(Box<Expr>, Box<Expr>),
    /// A call of a function the program defines, by its place.
    Call(usize, Vec<Expr>),
    Double(Box<Expr>),
    Show(Box<Expr>),
    /// `fmt(e)`, a call of the trait's function itself.
    Fmt(Box<Expr>),
    /// `apply(e) fn(p) { body }`.
    Apply(Box<Expr>, String, Box<Expr>),
}

#[derive(Clone, Debug)]
enum Part {
    Text(Vec<usize>),
    Expr(Expr),
}

#[derive(Clone, Debug)]
enum Stmt {
    Define(String, Expr),
    /// `(a, b) = e`, of a `(str, Cat)`.
    Destructure(String, String, Expr),
    /// `n = match e { .cat(c) { x } .none { y } }`, of a `Pet`, where both
    /// blocks give text.
    Match(String, Expr, String, Expr, Expr),
    Print(Expr),
}

#[derive(Clone, Debug)]
struct Func {
    name: String,
    params: Vec<(String, Ty)>,
    result: Ty,
    body: Vec<Stmt>,
    value: Expr,
}

#[derive(Clone, Debug)]
struct Model {
    /// The string `Fmt[Cat]` gives, with `value` the `Cat`.
    cat: Expr,
    /// The texts `Fmt[Pet]` gives for `.cat(c)` and `.none`, with `value`
    /// the `Pet`.
    pet: (Expr, Expr),
    /// The texts `apply` puts before and after what the function makes.
    apply: (Vec<usize>, Vec<usize>),
    /// The same, for `show`.
    show: (Vec<usize>, Vec<usize>),
    funcs: Vec<Func>,
    main: Vec<Stmt>,
}

#[derive(Clone, Debug, PartialEq, Eq)]
enum Val {
    Str(Vec<u8>),
    Cat(Vec<u8>),
    Pet(Option<Vec<u8>>),
    Pair(Vec<u8>, Vec<u8>),
}

impl Val {
    fn text(self) -> Vec<u8> {
        match self {
            Val::Str(s) => s,
            other => panic!("{other:?} is not text"),
        }
    }
}

/// How deep an expression is built.
const DEPTH: usize = 3;
/// The most a program may print, so that a run stays short.
const MOST: usize = 1 << 20;

fn text_of(pieces: &[usize]) -> Vec<u8> {
    pieces.iter().flat_map(|&p| PIECES[p].1.bytes()).collect()
}

fn pieces(g: &mut Gen) -> Vec<usize> {
    (0..g.below(3)).map(|_| g.below(PIECES.len())).collect()
}

/// The names defined so far in a function, and how many it has defined.
struct Scope {
    names: Vec<(String, Ty)>,
    count: usize,
}

impl Scope {
    fn fresh(&mut self, prefix: &str) -> String {
        self.count += 1;
        format!("{prefix}{}", self.count)
    }

    fn of(&self, ty: Ty) -> Vec<String> {
        (self.names.iter())
            .filter(|(_, t)| *t == ty)
            .map(|(n, _)| n.clone())
            .collect()
    }
}

/// An expression of the type, which calls only the first `callable`
/// functions.
fn expr(g: &mut Gen, funcs: &[Func], scope: &mut Scope, ty: Ty, depth: usize) -> Expr {
    let names = scope.of(ty);
    let calls: Vec<usize> = (0..funcs.len())
        .filter(|&k| funcs[k].result == ty)
        .collect();
    if depth == 0 || g.one_in(4) {
        if !names.is_empty() && g.one_in(2) {
            return Expr::Name(g.pick(&names).clone());
        }
        return match ty {
            Ty::Str => Expr::Lit(pieces(g)),
            Ty::Cat => Expr::Cat(Box::new(Expr::Lit(pieces(g)))),
            Ty::Pet => Expr::PetNone,
            Ty::Pair => Expr::Pair(
                Box::new(Expr::Lit(pieces(g))),
                Box::new(Expr::Cat(Box::new(Expr::Lit(pieces(g))))),
            ),
        };
    }
    let d = depth - 1;
    if !calls.is_empty() && g.one_in(4) {
        let k = *g.pick(&calls);
        let args = (funcs[k].params.iter())
            .map(|(_, t)| expr(g, funcs, scope, *t, d))
            .collect();
        return Expr::Call(k, args);
    }
    match ty {
        Ty::Str => match g.below(8) {
            0 => Expr::Lit(pieces(g)),
            1 => Expr::CatName(Box::new(expr(g, funcs, scope, Ty::Cat, d))),
            2 => Expr::Double(Box::new(expr(g, funcs, scope, Ty::Str, d))),
            3 => {
                let t = *g.pick(TYPES);
                Expr::Show(Box::new(expr(g, funcs, scope, t, d)))
            }
            4 => {
                let t = *g.pick(TYPES);
                Expr::Fmt(Box::new(expr(g, funcs, scope, t, d)))
            }
            5 => {
                let arg = expr(g, funcs, scope, Ty::Str, d);
                let param = scope.fresh("l");
                scope.names.push((param.clone(), Ty::Str));
                let body = expr(g, funcs, scope, Ty::Str, d);
                scope.names.pop();
                Expr::Apply(Box::new(arg), param, Box::new(body))
            }
            _ => interp(g, funcs, scope, d),
        },
        Ty::Cat => Expr::Cat(Box::new(expr(g, funcs, scope, Ty::Str, d))),
        Ty::Pet => Expr::PetCat(Box::new(expr(g, funcs, scope, Ty::Cat, d))),
        Ty::Pair => Expr::Pair(
            Box::new(expr(g, funcs, scope, Ty::Str, d)),
            Box::new(expr(g, funcs, scope, Ty::Cat, d)),
        ),
    }
}

/// A string that holds one expression or more, each of a type that
/// implements `Fmt`.
fn interp(g: &mut Gen, funcs: &[Func], scope: &mut Scope, depth: usize) -> Expr {
    let mut parts = Vec::new();
    for _ in 0..1 + g.below(3) {
        if g.one_in(2) {
            parts.push(Part::Text(pieces(g)));
        }
        let t = *g.pick(TYPES);
        parts.push(Part::Expr(expr(g, funcs, scope, t, depth)));
    }
    if g.one_in(2) {
        parts.push(Part::Text(pieces(g)));
    }
    Expr::Interp(parts)
}

/// Statements that bind names, take tuples and a `Pet` apart, and, in
/// `main`, print.
fn stmts(g: &mut Gen, funcs: &[Func], scope: &mut Scope, main: bool) -> Vec<Stmt> {
    let mut out = Vec::new();
    for _ in 0..g.below(if main { 6 } else { 3 }) {
        let choice = g.below(if main { 5 } else { 3 });
        let stmt = match choice {
            0 => {
                let t = *g.pick(TYPES);
                let value = expr(g, funcs, scope, t, DEPTH);
                let name = scope.fresh("t");
                scope.names.push((name.clone(), t));
                Stmt::Define(name, value)
            }
            1 => {
                let value = expr(g, funcs, scope, Ty::Pair, DEPTH);
                let (a, b) = (scope.fresh("s"), scope.fresh("c"));
                scope.names.push((a.clone(), Ty::Str));
                scope.names.push((b.clone(), Ty::Cat));
                Stmt::Destructure(a, b, value)
            }
            2 => {
                let of = expr(g, funcs, scope, Ty::Pet, DEPTH);
                let bound = scope.fresh("c");
                scope.names.push((bound.clone(), Ty::Cat));
                let cat = expr(g, funcs, scope, Ty::Str, DEPTH - 1);
                scope.names.pop();
                let none = expr(g, funcs, scope, Ty::Str, DEPTH - 1);
                let name = scope.fresh("t");
                scope.names.push((name.clone(), Ty::Str));
                Stmt::Match(name, of, bound, cat, none)
            }
            _ => Stmt::Print(expr(g, funcs, scope, Ty::Str, DEPTH)),
        };
        out.push(stmt);
    }
    out
}

fn model(g: &mut Gen) -> Model {
    let value = || Box::new(Expr::Name("value".to_string()));
    let mut cat = vec![Part::Expr(Expr::CatName(value()))];
    if g.one_in(2) {
        cat.insert(0, Part::Text(pieces(g)));
    }
    if g.one_in(2) {
        cat.push(Part::Expr(Expr::Lit(pieces(g))));
    }
    let c = || Expr::Name("c".to_string());
    let pet_cat = Expr::Interp(match g.below(3) {
        0 => vec![Part::Expr(c())],
        1 => vec![Part::Text(pieces(g)), Part::Expr(c())],
        _ => vec![
            Part::Expr(Expr::CatName(Box::new(c()))),
            Part::Text(pieces(g)),
            Part::Expr(c()),
        ],
    });
    let pet_none = Expr::Lit(pieces(g));
    let apply = (pieces(g), pieces(g));
    let show = (pieces(g), pieces(g));
    let mut funcs: Vec<Func> = Vec::new();
    for k in 0..g.below(4) {
        let mut scope = Scope {
            names: Vec::new(),
            count: 0,
        };
        for _ in 0..1 + g.below(2) {
            let t = *g.pick(TYPES);
            let name = scope.fresh("p");
            scope.names.push((name, t));
        }
        let params = scope.names.clone();
        let result = *g.pick(TYPES);
        let body = stmts(g, &funcs, &mut scope, false);
        let value = expr(g, &funcs, &mut scope, result, DEPTH);
        funcs.push(Func {
            name: format!("f{k}"),
            params,
            result,
            body,
            value,
        });
    }
    let mut scope = Scope {
        names: Vec::new(),
        count: 0,
    };
    let mut main = stmts(g, &funcs, &mut scope, true);
    if g.one_in(4) {
        // A text far larger than a page of memory.
        let mut big = Expr::Lit(vec![1 + g.below(PIECES.len() - 1)]);
        for _ in 0..12 + g.below(3) {
            big = Expr::Double(Box::new(big));
        }
        let at = g.below(main.len() + 1);
        main.insert(at, Stmt::Print(big));
    }
    Model {
        cat: Expr::Interp(cat),
        pet: (pet_cat, pet_none),
        apply,
        show,
        funcs,
        main,
    }
}

fn eval(m: &Model, env: &[(String, Val)], e: &Expr) -> Val {
    let text = |e: &Expr| eval(m, env, e).text();
    match e {
        Expr::Lit(p) => Val::Str(text_of(p)),
        Expr::Interp(parts) => {
            let mut out = Vec::new();
            for part in parts {
                match part {
                    Part::Text(p) => out.extend(text_of(p)),
                    Part::Expr(e) => out.extend(fmt(m, eval(m, env, e))),
                }
            }
            Val::Str(out)
        }
        Expr::Name(n) => (env.iter().rfind(|(m, _)| m == n))
            .unwrap_or_else(|| panic!("`{n}` is defined"))
            .1
            .clone(),
        Expr::Cat(e) => Val::Cat(text(e)),
        Expr::CatName(e) => match eval(m, env, e) {
            Val::Cat(n) => Val::Str(n),
            other => panic!("{other:?} is not a cat"),
        },
        Expr::PetCat(e) => match eval(m, env, e) {
            Val::Cat(n) => Val::Pet(Some(n)),
            other => panic!("{other:?} is not a cat"),
        },
        Expr::PetNone => Val::Pet(None),
        Expr::Pair(a, b) => match eval(m, env, b) {
            Val::Cat(n) => Val::Pair(text(a), n),
            other => panic!("{other:?} is not a cat"),
        },
        Expr::Call(k, args) => {
            let args = args.iter().map(|a| eval(m, env, a)).collect();
            call(m, *k, args)
        }
        Expr::Double(e) => Val::Str(text(e).repeat(2)),
        Expr::Show(e) => {
            let mut out = text_of(&m.show.0);
            out.extend(fmt(m, eval(m, env, e)));
            out.extend(text_of(&m.show.1));
            Val::Str(out)
        }
        Expr::Fmt(e) => Val::Str(fmt(m, eval(m, env, e))),
        Expr::Apply(arg, param, body) => {
            let mut inner = env.to_vec();
            inner.push((param.clone(), Val::Str(text(arg))));
            let mut out = text_of(&m.apply.0);
            out.extend(eval(m, &inner, body).text());
            out.extend(text_of(&m.apply.1));
            Val::Str(out)
        }
    }
}

/// What `Fmt` makes of a value, by the implementation its type chooses.
fn fmt(m: &Model, v: Val) -> Vec<u8> {
    match v {
        Val::Str(s) => s,
        Val::Cat(_) => eval(m, &[("value".to_string(), v)], &m.cat).text(),
        Val::Pet(Some(n)) => {
            let env = [
                ("value".to_string(), Val::Pet(Some(n.clone()))),
                ("c".to_string(), Val::Cat(n)),
            ];
            eval(m, &env, &m.pet.0).text()
        }
        Val::Pet(None) => eval(m, &[], &m.pet.1).text(),
        Val::Pair(s, n) => {
            let mut out = b"(".to_vec();
            out.extend(repr(&s));
            out.extend(b", Cat(name: ");
            out.extend(repr(&n));
            out.extend(b"))");
            out
        }
    }
}

/// The text `Repr` gives for a `str`: the text in quotes, with its escapes.
fn repr(text: &[u8]) -> Vec<u8> {
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

fn call(m: &Model, k: usize, args: Vec<Val>) -> Val {
    let f = &m.funcs[k];
    let mut env: Vec<(String, Val)> = (f.params.iter().map(|(n, _)| n.clone()))
        .zip(args)
        .collect();
    run(m, &f.body, &mut env, &mut Vec::new());
    eval(m, &env, &f.value)
}

fn run(m: &Model, stmts: &[Stmt], env: &mut Vec<(String, Val)>, out: &mut Vec<u8>) {
    for stmt in stmts {
        match stmt {
            Stmt::Define(n, e) => {
                let v = eval(m, env, e);
                env.push((n.clone(), v));
            }
            Stmt::Destructure(a, b, e) => {
                let Val::Pair(s, c) = eval(m, env, e) else {
                    panic!("a pair")
                };
                env.push((a.clone(), Val::Str(s)));
                env.push((b.clone(), Val::Cat(c)));
            }
            Stmt::Match(n, of, bound, cat, none) => {
                let v = match eval(m, env, of) {
                    Val::Pet(Some(c)) => {
                        let mut inner = env.clone();
                        inner.push((bound.clone(), Val::Cat(c)));
                        eval(m, &inner, cat)
                    }
                    Val::Pet(None) => eval(m, env, none),
                    other => panic!("{other:?} is not a pet"),
                };
                env.push((n.clone(), v));
            }
            Stmt::Print(e) => {
                out.extend(eval(m, env, e).text());
                out.push(b'\n');
            }
        }
    }
}

/// Whether evaluating the expression asks for memory, and so needs `alloc`
/// in the function it is written in.
fn allocates(m: &Model, e: &Expr) -> bool {
    let any = |es: &[&Expr]| es.iter().any(|e| allocates(m, e));
    match e {
        Expr::Interp(_) | Expr::Double(_) | Expr::Show(_) | Expr::Fmt(_) | Expr::Apply(..) => true,
        Expr::Call(k, args) => needs_alloc(m, &m.funcs[*k]) || args.iter().any(|a| allocates(m, a)),
        Expr::Lit(_) | Expr::Name(_) | Expr::PetNone => false,
        Expr::Cat(e) | Expr::CatName(e) | Expr::PetCat(e) => allocates(m, e),
        Expr::Pair(a, b) => any(&[a, b]),
    }
}

fn stmt_exprs(stmt: &Stmt) -> Vec<&Expr> {
    match stmt {
        Stmt::Define(_, e) | Stmt::Destructure(_, _, e) | Stmt::Print(e) => vec![e],
        Stmt::Match(_, of, _, a, b) => vec![of, a, b],
    }
}

fn needs_alloc(m: &Model, f: &Func) -> bool {
    (f.body.iter().flat_map(stmt_exprs))
        .chain([&f.value])
        .any(|e| allocates(m, e))
}

/// The functions each expression calls, directly.
fn called(e: &Expr, out: &mut Vec<usize>) {
    match e {
        Expr::Call(k, args) => {
            out.push(*k);
            for a in args {
                called(a, out);
            }
        }
        Expr::Interp(parts) => {
            for part in parts {
                if let Part::Expr(e) = part {
                    called(e, out);
                }
            }
        }
        Expr::Lit(_) | Expr::Name(_) | Expr::PetNone => {}
        Expr::Cat(e)
        | Expr::CatName(e)
        | Expr::PetCat(e)
        | Expr::Double(e)
        | Expr::Show(e)
        | Expr::Fmt(e) => called(e, out),
        Expr::Pair(a, b) | Expr::Apply(a, _, b) => {
            called(a, out);
            called(b, out);
        }
    }
}

/// The functions `main` reaches, which are the ones checked.
fn reached(m: &Model) -> Vec<usize> {
    let mut out = Vec::new();
    for e in m.main.iter().flat_map(stmt_exprs) {
        called(e, &mut out);
    }
    let mut i = 0;
    while i < out.len() {
        let f = &m.funcs[out[i]];
        let mut more = Vec::new();
        for e in f.body.iter().flat_map(stmt_exprs).chain([&f.value]) {
            called(e, &mut more);
        }
        for k in more {
            if !out.contains(&k) {
                out.push(k);
            }
        }
        i += 1;
    }
    out
}

/// The mistake to place, if any.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Mistake {
    /// A value of a struct whose `Fmt` is taken away in a string of `main`,
    /// before its statement at the place, or given to `show`.
    NoFmt { at: usize, shown: bool },
    /// The function, or `main` when there is none, written without
    /// `alloc`, which it needs.
    NoAlloc(Option<usize>),
    /// A function without `alloc` that calls one with it, called from
    /// `main` before its statement at the place.
    CallWithoutAlloc { at: usize },
}

/// The source as it is written, with where each function, by its place or
/// `main` after them, first needs `alloc`.
struct Writer<'m> {
    m: &'m Model,
    text: String,
    /// The function being written, if it is one of the model's or `main`.
    at: Option<usize>,
    /// For each function and then `main`, the places that need `alloc`.
    sites: Vec<Vec<Pos>>,
}

impl Writer<'_> {
    fn push(&mut self, s: &str) {
        self.text.push_str(s);
    }

    /// Notes that what is written next needs `alloc`.
    fn site(&mut self) {
        if let Some(f) = self.at {
            let pos = pos_after(&self.text);
            self.sites[f].push(pos);
        }
    }

    fn pieces(&mut self, p: &[usize]) {
        for &i in p {
            self.push(PIECES[i].0);
        }
    }

    fn expr(&mut self, e: &Expr) {
        match e {
            Expr::Lit(p) => {
                self.push("\"");
                self.pieces(p);
                self.push("\"");
            }
            Expr::Interp(parts) => {
                self.site();
                self.push("\"");
                for part in parts {
                    match part {
                        Part::Text(p) => self.pieces(p),
                        Part::Expr(e) => {
                            self.push("{");
                            self.expr(e);
                            self.push("}");
                        }
                    }
                }
                self.push("\"");
            }
            Expr::Name(n) => self.push(n),
            Expr::Cat(e) => self.call("Cat(name: ", &[e], ")"),
            Expr::CatName(e) => {
                self.expr(e);
                self.push(".name");
            }
            Expr::PetCat(e) => self.call("Pet.cat(", &[e], ")"),
            Expr::PetNone => self.push("Pet.none"),
            Expr::Pair(a, b) => self.call("(", &[a, b], ")"),
            Expr::Call(k, args) => {
                if needs_alloc(self.m, &self.m.funcs[*k]) {
                    self.site();
                }
                let open = format!("{}(", self.m.funcs[*k].name);
                let args: Vec<&Expr> = args.iter().collect();
                self.call(&open, &args, ")");
            }
            Expr::Double(e) => {
                self.site();
                self.call("double(", &[e], ")");
            }
            Expr::Show(e) => {
                self.site();
                self.call("show(", &[e], ")");
            }
            Expr::Fmt(e) => {
                self.site();
                self.call("fmt(", &[e], ")");
            }
            Expr::Apply(arg, param, body) => {
                self.site();
                self.call("apply(", &[arg], ")");
                self.push(&format!(" fn({param}) {{ "));
                self.expr(body);
                self.push(" }");
            }
        }
    }

    fn call(&mut self, open: &str, args: &[&Expr], close: &str) {
        self.push(open);
        for (i, a) in args.iter().enumerate() {
            if i > 0 {
                self.push(", ");
            }
            self.expr(a);
        }
        self.push(close);
    }

    fn stmt(&mut self, stmt: &Stmt) {
        self.push("  ");
        match stmt {
            Stmt::Define(n, e) => {
                self.push(&format!("{n} = "));
                self.expr(e);
            }
            Stmt::Destructure(a, b, e) => {
                self.push(&format!("({a}, {b}) = "));
                self.expr(e);
            }
            Stmt::Match(n, of, bound, cat, none) => {
                self.push(&format!("{n} = match "));
                self.expr(of);
                self.push(&format!(" {{\n    .cat({bound}) {{ "));
                self.expr(cat);
                self.push(" }\n    .none { ");
                self.expr(none);
                self.push(" }\n  }");
            }
            Stmt::Print(e) => self.call("print(", &[e], ")"),
        }
        self.push("\n");
    }
}

/// Writes the model out with the mistake, and gives the source and the
/// error the mistake must give.
fn write(m: &Model, mistake: Option<Mistake>) -> (String, Option<Expected>) {
    let main = m.funcs.len();
    let relay = main + 1;
    let mut w = Writer {
        m,
        text: String::new(),
        at: None,
        sites: vec![Vec::new(); relay + 1],
    };
    w.push("struct Cat {\n  name: str\n}\n\nenum Pet {\n  cat: Cat\n  none\n}\n\n");
    if let Some(Mistake::NoFmt { .. }) = mistake {
        w.push("struct Dog {\n  name: str\n}\n\nwithout Fmt for Dog\n\n");
    }
    w.push("impl Fmt[Cat] {\n  fn fmt(value: Cat) -> str ! alloc { ");
    w.expr(&m.cat);
    w.push(" }\n}\n\nimpl Fmt[Pet] {\n  fn fmt(value: Pet) -> str ! alloc {\n");
    w.push("    match value {\n      .cat(c) { ");
    w.expr(&m.pet.0);
    w.push(" }\n      .none { ");
    w.expr(&m.pet.1);
    w.push(" }\n    }\n  }\n}\n\n");
    w.push("fn double(s: str) -> str ! alloc { \"{s}{s}\" }\n\n");
    w.push("fn apply(s: str, f: fn(str) -> str) -> str ! alloc { \"");
    w.pieces(&m.apply.0);
    w.push("{f(s)}");
    w.pieces(&m.apply.1);
    w.push("\" }\n\nfn show[T: Fmt](x: T) -> str ! alloc { \"");
    w.pieces(&m.show.0);
    w.push("{x}");
    w.pieces(&m.show.1);
    w.push("\" }\n\n");
    for (k, f) in m.funcs.iter().enumerate() {
        w.at = Some(k);
        let params: Vec<String> = (f.params.iter())
            .map(|(n, t)| format!("{n}: {}", t.written()))
            .collect();
        w.push(&format!(
            "fn {}({}) -> {}",
            f.name,
            params.join(", "),
            f.result.written()
        ));
        if needs_alloc(m, f) && mistake != Some(Mistake::NoAlloc(Some(k))) {
            w.push(" ! alloc");
        }
        w.push(" {\n");
        for stmt in &f.body {
            w.stmt(stmt);
        }
        w.push("  ");
        w.expr(&f.value);
        w.push("\n}\n\n");
    }
    if let Some(Mistake::CallWithoutAlloc { .. }) = mistake {
        w.at = Some(relay);
        w.push("fn relay() -> str { ");
        w.site();
        w.push("double(\"a\") }\n\n");
    }
    w.at = Some(main);
    w.push("fn main() ! console");
    if mistake != Some(Mistake::NoAlloc(None)) {
        w.push(", alloc");
    }
    w.push(" {\n");
    let mut placed = None;
    for i in 0..=m.main.len() {
        match mistake {
            Some(Mistake::NoFmt { at, shown }) if at == i => {
                if shown {
                    w.push("  print(");
                    placed = Some(pos_after(&w.text));
                    w.push("show(Dog(name: \"a\")))\n");
                } else {
                    w.push("  print(\"a {");
                    placed = Some(pos_after(&w.text));
                    w.push("Dog(name: \"b\")} c\")\n");
                }
            }
            Some(Mistake::CallWithoutAlloc { at }) if at == i => {
                w.push("  print(relay())\n");
            }
            _ => {}
        }
        if let Some(stmt) = m.main.get(i) {
            w.stmt(stmt);
        }
    }
    w.push("}\n");
    let expected = match mistake {
        None => None,
        Some(Mistake::NoFmt { .. }) => Some(Expected {
            at: placed.expect("placed"),
            mentions: "no implementation of `Fmt` for `Dog`".to_string(),
            fix: None,
        }),
        Some(Mistake::NoAlloc(f)) => Some(Expected {
            at: w.sites[f.unwrap_or(main)][0],
            mentions: "needs the `alloc` effect".to_string(),
            fix: Some("alloc".to_string()),
        }),
        Some(Mistake::CallWithoutAlloc { .. }) => Some(Expected {
            at: w.sites[relay][0],
            mentions: "`double` needs the `alloc` effect".to_string(),
            fix: Some("fn relay() -> str ! alloc".to_string()),
        }),
    };
    (w.text, expected)
}

/// What `main` prints, and what it prints before its statement at `at`.
fn printed(m: &Model, at: usize) -> (Vec<u8>, Vec<u8>) {
    let mut before = Vec::new();
    run(m, &m.main[..at], &mut Vec::new(), &mut before);
    let mut all = Vec::new();
    run(m, &m.main, &mut Vec::new(), &mut all);
    (all, before)
}

/// A model that prints no more than `MOST`.
fn small_model(g: &mut Gen) -> Model {
    loop {
        let m = model(g);
        if printed(&m, 0).0.len() <= MOST {
            return m;
        }
    }
}

/// A valid program whose strings hold expressions.
pub fn program(g: &mut Gen) -> TextCase {
    let m = small_model(g);
    let (source, _) = write(&m, None);
    TextCase {
        source,
        stdout: printed(&m, 0).0,
        mistake: None,
    }
}

/// A program whose strings hold expressions, with one mistake.
pub fn program_with_mistake(g: &mut Gen) -> TextCase {
    let m = small_model(g);
    let needing: Vec<usize> = (reached(&m).into_iter())
        .filter(|&k| needs_alloc(&m, &m.funcs[k]))
        .collect();
    let main_needs = m.main.iter().flat_map(stmt_exprs).any(|e| allocates(&m, e));
    let at = g.below(m.main.len() + 1);
    let mistake = match g.below(3) {
        0 if !needing.is_empty() || main_needs => {
            if main_needs && (needing.is_empty() || g.one_in(3)) {
                Mistake::NoAlloc(None)
            } else {
                Mistake::NoAlloc(Some(*g.pick(&needing)))
            }
        }
        1 => Mistake::CallWithoutAlloc { at },
        _ => Mistake::NoFmt {
            at,
            shown: g.one_in(2),
        },
    };
    let (source, mistake) = write(&m, Some(mistake));
    TextCase {
        source,
        stdout: Vec::new(),
        mistake,
    }
}

/// A valid program with a statement in `main` that builds a text of
/// `bytes` bytes or more, and what the program prints before that
/// statement.
pub fn program_with_huge_text(g: &mut Gen, bytes: usize) -> (TextCase, Vec<u8>) {
    let mut m = small_model(g);
    let at = g.below(m.main.len() + 1);
    let mut huge = Expr::Lit(vec![1]);
    let mut size = PIECES[1].1.len();
    while size < bytes {
        huge = Expr::Double(Box::new(huge));
        size *= 2;
    }
    let (_, before) = printed(&m, at);
    m.main.insert(at, Stmt::Print(huge));
    let (source, _) = write(&m, None);
    let case = TextCase {
        source,
        stdout: Vec::new(),
        mistake: None,
    };
    (case, before)
}
