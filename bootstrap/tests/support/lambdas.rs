//! The generator of programs whose functions take functions, which can also
//! place one mistake in what it builds.
//!
//! A program has a struct `Cat`, an enum `Pet` whose case `cat` holds a
//! `Cat`, `say`, which prints, and a few pure functions with a name, each
//! called once from `main` and some passed as values. A few functions take
//! values and functions, some of them with a type parameter `T`, and some
//! carry `console`. Their parameters' types and their lambdas' types are
//! text, a `Cat`, a `Pet`, `()`, a tuple, and `T`. Each calls the functions
//! it is given, takes a `Pet` apart with `match` and calls them in its
//! blocks, and calls the functions after it, passing them lambdas, the
//! functions it was given, or functions with a name. A lambda takes no
//! parameter or several, is written after the call, with no parentheses
//! when it is the only argument, or by name inside them, and may assign a
//! `var` of the function it is written in and call a function with a lambda
//! in turn. `main` calls the functions in the same ways.
//!
//! The generator first builds a model of the program and works out from it
//! what the program prints. Then it writes the model out, placing the
//! mistake as it writes.

use std::cell::RefCell;
use std::rc::Rc;

use crate::generate::Gen;
use crate::programs::{Expected, PIECES, pos_after};

/// A program whose functions take functions: its source, what it prints,
/// how many functions the scalar form holds, and the
/// mistake placed in it, if any.
pub struct LambdaCase {
    pub source: String,
    /// The same program with every expression in parentheses, for a valid
    /// one.
    pub parenthesized: String,
    /// The same program with each lambda writing the types of its
    /// parameters, the type of its result, and both, for a valid one.
    pub typed: Vec<String>,
    pub stdout: Vec<u8>,
    pub copies: usize,
    pub mistake: Option<Expected>,
}

impl std::fmt::Debug for LambdaCase {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self.source)
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
enum Ty {
    Str,
    Cat,
    Pet,
    /// The empty tuple, `()`.
    Unit,
    /// `(str, Cat)`.
    Pair,
    /// The type parameter of the function the type is written in.
    T,
}

/// The type of a function: its parameters' and its result's.
#[derive(Clone, Debug, PartialEq, Eq)]
struct FnTy {
    params: Vec<Ty>,
    result: Ty,
}

/// The pure functions with a name, each with its type and its body. `main`
/// calls each once, and any may be passed as a value.
const PLAIN: &[(&str, &[Ty], Ty, &str)] = &[
    ("name_of", &[Ty::Cat], Ty::Str, "a0.name"),
    ("cat_of", &[Ty::Str], Ty::Cat, "Cat(name: a0)"),
    ("echo", &[Ty::Str], Ty::Str, "a0"),
    ("pet_of", &[Ty::Cat], Ty::Pet, ".cat(a0)"),
    ("join", &[Ty::Str, Ty::Cat], Ty::Pair, "(a0, a1)"),
    ("first", &[Ty::Pair], Ty::Str, "(s, c) = a0\n  s"),
    ("second", &[Ty::Pair], Ty::Cat, "(s, c) = a0\n  c"),
    ("drop", &[Ty::Str], Ty::Unit, ""),
];

fn plain_ty(k: usize) -> FnTy {
    FnTy {
        params: PLAIN[k].1.to_vec(),
        result: PLAIN[k].2.clone(),
    }
}

#[derive(Clone, Debug)]
struct LambdaM {
    params: Vec<String>,
    /// The type of each parameter, and of the result.
    types: Vec<Ty>,
    result: Ty,
    body: Vec<Stmt>,
}

#[derive(Clone, Debug)]
enum Expr {
    /// A literal, as the places of its pieces in `PIECES`.
    Lit(Vec<usize>),
    Name(String),
    /// The `name` of a `Cat`.
    Field(Box<Expr>),
    Cat(Box<Expr>),
    /// `Pet.cat(...)` or `Pet.none`.
    Pet(Option<Box<Expr>>),
    Unit,
    Pair(Box<Expr>, Box<Expr>),
    Say(Box<Expr>),
    /// A call of a pure function with a name.
    Plain(usize, Vec<Expr>),
    /// A pure function with a name, passed as a value.
    PlainValue(usize),
    /// A call of a parameter or a lambda's parameter that holds a function.
    CallValue(String, Vec<Expr>),
    /// A call of function `h`, with its arguments in the order they are
    /// written.
    Call(usize, Vec<Arg>),
    Lambda(Rc<LambdaM>),
    /// A `match` on a `Pet`: the name `.cat` binds, and what each block
    /// gives.
    Match(Box<Expr>, String, Box<Expr>, Box<Expr>),
}

/// An argument: the parameter it is for, by its place and its name, and
/// how it is written.
#[derive(Clone, Debug)]
struct Arg {
    param: usize,
    name: String,
    how: How,
    value: Expr,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum How {
    Position,
    Named,
    /// A lambda written after the call.
    After,
}

#[derive(Clone, Debug)]
enum Stmt {
    Define(String, Expr),
    Var(String, Expr),
    Assign(String, Expr),
    Print(Expr),
    Expr(Expr),
    /// Lines written as they are, with the mistake placed after the first
    /// part: what it mentions and what its fix mentions, if it has one.
    Raw(String, String, String, Option<String>),
}

/// A function that takes functions.
struct Ho {
    generic: bool,
    values: Vec<Ty>,
    fns: Vec<FnTy>,
    result: Ty,
    console: bool,
    body: Vec<Stmt>,
}

impl Ho {
    /// The type of each parameter: the values, then the functions.
    fn params(&self) -> Vec<Result<Ty, FnTy>> {
        let values = self.values.iter().cloned().map(Ok);
        values.chain(self.fns.iter().cloned().map(Err)).collect()
    }

    fn param_name(&self, i: usize) -> String {
        if i < self.values.len() {
            format!("p{i}")
        } else {
            format!("f{}", i - self.values.len())
        }
    }
}

/// A definition the mistake needs, written after the others, with the
/// mistake placed after its first part.
struct Item(String, String, String, Option<String>);

struct Model {
    hos: Vec<Ho>,
    main: Vec<Stmt>,
    item: Option<Item>,
}

/// The kinds of mistake the generator can place, each once in a file.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Kind {
    Bound,
    InField,
    Returned,
    Alone,
    Count,
    WrongValue,
    TakesNoFunction,
    EmptyFn,
    Effect,
    MatchCall,
}

const KINDS: &[Kind] = &[
    Kind::Bound,
    Kind::InField,
    Kind::Returned,
    Kind::Alone,
    Kind::Count,
    Kind::WrongValue,
    Kind::TakesNoFunction,
    Kind::EmptyFn,
    Kind::Effect,
    Kind::MatchCall,
];

/// A valid program.
pub fn program(g: &mut Gen) -> LambdaCase {
    build(g, None)
}

/// A program with one mistake in it.
pub fn program_with_mistake(g: &mut Gen) -> LambdaCase {
    let kind = *g.pick(KINDS);
    build(g, Some(kind))
}

/// The type with `T` replaced by `t`.
fn subst(ty: &Ty, t: &Ty) -> Ty {
    if *ty == Ty::T { t.clone() } else { ty.clone() }
}

fn subst_fn(f: &FnTy, t: &Ty) -> FnTy {
    FnTy {
        params: f.params.iter().map(|p| subst(p, t)).collect(),
        result: subst(&f.result, t),
    }
}

/// What generating code needs to know: the names defined, the `var`s among
/// them, the parameters that hold functions, the functions it may call,
/// whether it may print, and whether `T` is a type there.
#[derive(Clone)]
struct Ctx {
    names: Vec<(String, Ty)>,
    vars: Vec<String>,
    fns: Vec<(String, FnTy)>,
    callable: std::ops::Range<usize>,
    console: bool,
    generic: bool,
}

impl Ctx {
    fn named(&self, ty: &Ty) -> Vec<String> {
        (self.names.iter())
            .filter(|(_, t)| t == ty)
            .map(|(n, _)| n.clone())
            .collect()
    }
}

struct Writing<'a> {
    g: &'a mut Gen,
    hos: &'a [Ho],
    fresh: usize,
}

impl Writing<'_> {
    fn fresh(&mut self, prefix: &str) -> String {
        self.fresh += 1;
        format!("{prefix}{}", self.fresh)
    }

    fn literal(&mut self) -> Expr {
        let g = &mut *self.g;
        Expr::Lit((0..g.below(3)).map(|_| g.below(PIECES.len())).collect())
    }

    /// A type that may be written where `ctx` is.
    fn some_type(&mut self, ctx: &Ctx) -> Ty {
        let types = [Ty::Str, Ty::Cat, Ty::Pet, Ty::Unit, Ty::Pair, Ty::T];
        let n = if ctx.generic { 6 } else { 5 };
        types[self.g.below(n)].clone()
    }

    /// An expression of the type.
    fn value(&mut self, ctx: &Ctx, ty: &Ty, depth: usize) -> Expr {
        let named = ctx.named(ty);
        if !named.is_empty() && (self.g.one_in(3) || *ty == Ty::T && depth == 0) {
            return Expr::Name(self.g.pick(&named).clone());
        }
        if depth > 0 {
            match self.g.below(5) {
                0 => {
                    if let Some(e) = self.call_value(ctx, ty, depth) {
                        return e;
                    }
                }
                1 => {
                    if let Some(e) = self.call(ctx, ty, depth) {
                        return e;
                    }
                }
                2 => {
                    let of = self.value(ctx, &Ty::Pet, depth - 1);
                    let bound = self.fresh("c");
                    let mut inner = ctx.clone();
                    inner.names.push((bound.clone(), Ty::Cat));
                    let cat = self.value(&inner, ty, depth - 1);
                    let none = self.value(ctx, ty, depth - 1);
                    return Expr::Match(Box::new(of), bound, Box::new(cat), Box::new(none));
                }
                3 => {
                    let fits: Vec<usize> =
                        (0..PLAIN.len()).filter(|k| PLAIN[*k].2 == *ty).collect();
                    if !fits.is_empty() {
                        let k = *self.g.pick(&fits);
                        let args = (PLAIN[k].1.iter())
                            .map(|t| self.value(ctx, t, depth - 1))
                            .collect();
                        return Expr::Plain(k, args);
                    }
                }
                _ => {}
            }
        }
        let inner = depth.saturating_sub(1);
        match ty {
            Ty::Str => match self.g.below(3) {
                0 if !ctx.named(&Ty::Cat).is_empty() => {
                    let cats = ctx.named(&Ty::Cat);
                    Expr::Field(Box::new(Expr::Name(self.g.pick(&cats).clone())))
                }
                _ => self.literal(),
            },
            Ty::Cat => Expr::Cat(Box::new(self.value(ctx, &Ty::Str, inner))),
            Ty::Pet => {
                if self.g.one_in(2) {
                    Expr::Pet(None)
                } else {
                    Expr::Pet(Some(Box::new(self.value(ctx, &Ty::Cat, inner))))
                }
            }
            Ty::Unit => {
                if ctx.console && self.g.one_in(2) {
                    Expr::Say(Box::new(self.value(ctx, &Ty::Str, inner)))
                } else {
                    Expr::Unit
                }
            }
            Ty::Pair => Expr::Pair(
                Box::new(self.value(ctx, &Ty::Str, inner)),
                Box::new(self.value(ctx, &Ty::Cat, inner)),
            ),
            Ty::T => Expr::Name(self.g.pick(&named).clone()),
        }
    }

    /// A call of a parameter that holds a function that gives `ty`, if
    /// there is one.
    fn call_value(&mut self, ctx: &Ctx, ty: &Ty, depth: usize) -> Option<Expr> {
        let fits: Vec<(String, FnTy)> = (ctx.fns.iter())
            .filter(|(_, f)| f.result == *ty)
            .cloned()
            .collect();
        if fits.is_empty() {
            return None;
        }
        let (name, f) = self.g.pick(&fits).clone();
        let args = (f.params.iter())
            .map(|t| self.value(ctx, t, depth - 1))
            .collect();
        Some(Expr::CallValue(name, args))
    }

    /// A call of a function that takes functions that gives `ty`, if there
    /// is one that `ctx` may call.
    fn call(&mut self, ctx: &Ctx, ty: &Ty, depth: usize) -> Option<Expr> {
        // Each function that fits, with the type that stands for its `T`.
        let mut fits = Vec::new();
        for h in ctx.callable.clone() {
            let ho = &self.hos[h];
            if ho.console && !ctx.console {
                continue;
            }
            if !ho.generic && ho.result == *ty {
                fits.push((h, Ty::Unit));
            } else if ho.generic && ho.result == Ty::T {
                fits.push((h, ty.clone()));
            } else if ho.generic && ho.result == *ty {
                let t = self.some_type(ctx);
                fits.push((h, t));
            }
        }
        if fits.is_empty() {
            return None;
        }
        let (h, t) = self.g.pick(&fits).clone();
        Some(self.call_of(ctx, h, &t, depth))
    }

    /// A call of function `h`, whose `T` is `t`, with arguments in an order
    /// it allows: the values by position, a function by name, and the last
    /// function after the call when it is a lambda, or by name.
    fn call_of(&mut self, ctx: &Ctx, h: usize, t: &Ty, depth: usize) -> Expr {
        let hos = self.hos;
        let ho = &hos[h];
        let params = ho.params();
        let mut args = Vec::new();
        for (i, p) in params.iter().enumerate() {
            match p {
                Ok(ty) => args.push(Arg {
                    param: i,
                    name: ho.param_name(i),
                    how: How::Position,
                    value: self.value(ctx, &subst(ty, t), depth - 1),
                }),
                Err(f) => {
                    let value = self.function(ctx, &subst_fn(f, t), depth - 1);
                    let last = i + 1 == params.len();
                    let how = if last && matches!(value, Expr::Lambda(_)) && !self.g.one_in(4) {
                        How::After
                    } else {
                        How::Named
                    };
                    args.push(Arg {
                        param: i,
                        name: ho.param_name(i),
                        how,
                        value,
                    });
                }
            }
        }
        // The functions given by name may come in any order.
        let named: Vec<usize> = (0..args.len())
            .filter(|i| args[*i].how == How::Named)
            .collect();
        if named.len() == 2 && self.g.one_in(2) {
            args.swap(named[0], named[1]);
        }
        Expr::Call(h, args)
    }

    /// An expression that gives a function of the type: a lambda, a
    /// parameter that holds one, or a function with a name.
    fn function(&mut self, ctx: &Ctx, f: &FnTy, depth: usize) -> Expr {
        let passed: Vec<String> = (ctx.fns.iter())
            .filter(|(_, g)| g == f)
            .map(|(n, _)| n.clone())
            .collect();
        let plain: Vec<usize> = (0..PLAIN.len()).filter(|k| plain_ty(*k) == *f).collect();
        match self.g.below(4) {
            0 if !passed.is_empty() => Expr::Name(self.g.pick(&passed).clone()),
            1 if !plain.is_empty() => Expr::PlainValue(*self.g.pick(&plain)),
            _ => Expr::Lambda(Rc::new(self.lambda(ctx, f, depth))),
        }
    }

    fn lambda(&mut self, ctx: &Ctx, f: &FnTy, depth: usize) -> LambdaM {
        let mut inner = ctx.clone();
        let params: Vec<String> = f.params.iter().map(|_| self.fresh("a")).collect();
        for (name, ty) in params.iter().zip(&f.params) {
            inner.names.push((name.clone(), ty.clone()));
        }
        let mut body = Vec::new();
        if !inner.vars.is_empty() && self.g.one_in(3) {
            let name = self.g.pick(&inner.vars).clone();
            let ty = (inner.names.iter().find(|(n, _)| *n == name))
                .expect("defined")
                .1
                .clone();
            body.push(Stmt::Assign(name, self.value(&inner, &ty, depth)));
        }
        for _ in 0..self.g.below(3) {
            body.push(self.statement(&mut inner, depth));
        }
        if f.result != Ty::Unit {
            body.push(Stmt::Expr(self.value(&inner, &f.result, depth)));
        } else if self.g.one_in(2) {
            body.push(Stmt::Expr(self.value(&inner, &Ty::Unit, depth)));
        }
        LambdaM {
            params,
            types: f.params.clone(),
            result: f.result.clone(),
            body,
        }
    }

    /// A statement, with what it defines added to `ctx`.
    fn statement(&mut self, ctx: &mut Ctx, depth: usize) -> Stmt {
        match self.g.below(5) {
            0 if ctx.console => Stmt::Print(self.value(ctx, &Ty::Str, depth)),
            1 if !ctx.vars.is_empty() => {
                let name = self.g.pick(&ctx.vars).clone();
                let ty = (ctx.names.iter().find(|(n, _)| *n == name))
                    .expect("defined")
                    .1
                    .clone();
                Stmt::Assign(name, self.value(ctx, &ty, depth))
            }
            2 => Stmt::Expr(self.value(ctx, &Ty::Unit, depth)),
            3 => {
                let ty = self.some_type(ctx);
                let value = self.value(ctx, &ty, depth);
                let name = self.fresh("v");
                ctx.names.push((name.clone(), ty));
                ctx.vars.push(name.clone());
                Stmt::Var(name, value)
            }
            _ => {
                let ty = self.some_type(ctx);
                let value = self.value(ctx, &ty, depth);
                let name = self.fresh("v");
                ctx.names.push((name.clone(), ty));
                Stmt::Define(name, value)
            }
        }
    }
}

fn model(g: &mut Gen) -> Model {
    let count = 1 + g.below(3);
    let mut hos: Vec<Ho> = Vec::new();
    for _ in 0..count {
        let generic = g.one_in(3);
        let types = [Ty::Str, Ty::Cat, Ty::Pet, Ty::Unit, Ty::Pair, Ty::T];
        let n = if generic { 6 } else { 5 };
        let mut values: Vec<Ty> = (0..g.below(3)).map(|_| types[g.below(n)].clone()).collect();
        if generic {
            values.insert(0, Ty::T);
        }
        // The type of a function an earlier function was given, which it
        // can then pass on, or of a function with a name, where they fit.
        let earlier: Vec<FnTy> = (hos.iter())
            .filter(|h| h.generic == generic)
            .flat_map(|h| h.fns.clone())
            .collect();
        let fns = (0..1 + g.below(2))
            .map(|_| match g.below(4) {
                0 if !earlier.is_empty() => g.pick(&earlier).clone(),
                1 => plain_ty(g.below(PLAIN.len())),
                _ => FnTy {
                    params: (0..g.below(4)).map(|_| types[g.below(n)].clone()).collect(),
                    result: types[g.below(n)].clone(),
                },
            })
            .collect();
        hos.push(Ho {
            generic,
            values,
            fns,
            result: types[g.below(n)].clone(),
            console: g.one_in(2),
            body: Vec::new(),
        });
    }
    let mut fresh = 0;
    let mut bodies = Vec::new();
    for h in 0..count {
        let ho = &hos[h];
        let mut ctx = Ctx {
            names: (0..ho.values.len())
                .map(|i| (ho.param_name(i), ho.values[i].clone()))
                .collect(),
            vars: Vec::new(),
            fns: (ho.fns.iter().enumerate())
                .map(|(i, f)| (format!("f{i}"), f.clone()))
                .collect(),
            callable: h + 1..count,
            console: ho.console,
            generic: ho.generic,
        };
        let mut w = Writing {
            g: &mut *g,
            hos: &hos,
            fresh,
        };
        let mut body = Vec::new();
        // A `var` that the lambdas written here may assign.
        if w.g.one_in(2) {
            let name = w.fresh("v");
            body.push(Stmt::Var(name.clone(), w.literal()));
            ctx.names.push((name.clone(), Ty::Str));
            ctx.vars.push(name);
        }
        for _ in 0..w.g.below(3) {
            body.push(w.statement(&mut ctx, 2));
        }
        // Each function it is given is called, in each block of a `match`
        // if there is a `Pet` to take apart.
        for (name, f) in ctx.fns.clone() {
            let call = |w: &mut Writing| {
                let args = f.params.iter().map(|t| w.value(&ctx, t, 1)).collect();
                Expr::CallValue(name.clone(), args)
            };
            let pets = ctx.named(&Ty::Pet);
            let e = if !pets.is_empty() && w.g.one_in(2) {
                let pet = w.g.pick(&pets).clone();
                let bound = w.fresh("c");
                let of = Box::new(Expr::Name(pet));
                Expr::Match(of, bound, Box::new(call(&mut w)), Box::new(call(&mut w)))
            } else {
                call(&mut w)
            };
            if f.result == Ty::Unit {
                body.push(Stmt::Expr(e));
            } else {
                let v = w.fresh("v");
                ctx.names.push((v.clone(), f.result.clone()));
                body.push(Stmt::Define(v, e));
            }
            for _ in 0..w.g.below(2) {
                body.push(w.statement(&mut ctx, 2));
            }
        }
        let result = ho.result.clone();
        if result != Ty::Unit || w.g.one_in(2) {
            body.push(Stmt::Expr(w.value(&ctx, &result, 2)));
        }
        fresh = w.fresh;
        bodies.push(body);
    }
    for (ho, body) in hos.iter_mut().zip(bodies) {
        ho.body = body;
    }
    let mut ctx = Ctx {
        names: vec![("note".to_string(), Ty::Str)],
        vars: vec!["note".to_string()],
        fns: Vec::new(),
        callable: 0..count,
        console: true,
        generic: false,
    };
    let mut w = Writing {
        g,
        hos: &hos,
        fresh,
    };
    let mut main = vec![Stmt::Var("note".to_string(), w.literal())];
    // Each function with a name is called once from `main`.
    main.push(Stmt::Expr(Expr::Say(Box::new(w.literal()))));
    for (k, (_, params, result, _)) in PLAIN.iter().enumerate() {
        let args = params.iter().map(|t| w.value(&ctx, t, 1)).collect();
        let name = w.fresh("u");
        ctx.names.push((name.clone(), result.clone()));
        main.push(Stmt::Define(name, Expr::Plain(k, args)));
    }
    for (h, ho) in hos.iter().enumerate() {
        let t = w.some_type(&ctx);
        let call = w.call_of(&ctx, h, &t, 3);
        let result = subst(&ho.result, &t);
        if result == Ty::Str && w.g.one_in(2) {
            main.push(Stmt::Print(call));
        } else {
            let name = w.fresh("r");
            ctx.names.push((name.clone(), result));
            main.push(Stmt::Define(name, call));
        }
        for _ in 0..w.g.below(3) {
            main.push(w.statement(&mut ctx, 2));
        }
    }
    Model {
        hos,
        main,
        item: None,
    }
}

/// Lines for `main` that hold one mistake of the kind, and a definition of
/// its own if it needs one.
fn mistake(g: &mut Gen, kind: Kind) -> (Stmt, Option<Item>) {
    let raw = |before: &str, rest: &str, mentions: &str, fix: Option<&str>| {
        Stmt::Raw(
            before.to_string(),
            rest.to_string(),
            mentions.to_string(),
            fix.map(str::to_string),
        )
    };
    let item = |before: &str, rest: &str, mentions: &str, fix: Option<&str>| {
        Some(Item(
            before.to_string(),
            rest.to_string(),
            mentions.to_string(),
            fix.map(str::to_string),
        ))
    };
    match kind {
        Kind::Bound => {
            let (before, rest) = *g.pick(&[
                ("w = ", "{ \"a\" }"),
                ("w = ", "fn(t) { t }"),
                ("var w = ", "{ say(\"a\") }"),
                ("note = ", "{ \"a\" }"),
            ]);
            (raw(before, rest, "bound to a name", None), None)
        }
        Kind::InField => {
            let (before, rest) = *g.pick(&[
                ("w = Cat(name: ", "{ \"a\" })"),
                ("w = Cat(name: \"a\") ", "{ \"b\" }"),
            ]);
            (raw(before, rest, "stored in a field", None), None)
        }
        Kind::Returned => {
            if g.one_in(2) {
                (
                    raw("w = spare()", "", "", None),
                    item("fn spare() -> str {\n  ", "{ \"a\" }\n}", "returned", None),
                )
            } else {
                (
                    raw(
                        "w = apply(\"a\") fn(t) {\n    ",
                        "{ t }\n  }",
                        "returned",
                        None,
                    ),
                    None,
                )
            }
        }
        Kind::Alone => {
            let rest = *g.pick(&["{ say(\"a\") }", "fn(t) { say(t) }"]);
            (raw("", rest, "stands alone", None), None)
        }
        Kind::Count => {
            let (before, rest, takes) = *g.pick(&[
                ("w = apply(\"a\") ", "{ \"b\" }", "takes 0 parameters"),
                ("w = apply(\"a\") ", "fn(t, u) { t }", "takes 2 parameters"),
                ("twice ", "fn(t) { say(t) }", "takes 1 parameter"),
            ]);
            (raw(before, rest, takes, None), None)
        }
        Kind::WrongValue => {
            let (before, rest, gives) = *g.pick(&[
                (
                    "w = apply(\"a\") fn(t) { ",
                    "Cat(name: t) }",
                    "gives a `Cat`",
                ),
                ("w = apply(\"a\") fn(t) { ", "() }", "gives nothing"),
                ("twice { ", "\"a\" }", "gives text"),
                ("w = apply(\"a\", ", "cat_of)", "gives a `fn(str) -> Cat`"),
            ]);
            (raw(before, rest, gives, None), None)
        }
        Kind::TakesNoFunction => {
            let (before, rest) = *g.pick(&[
                ("w = echo(\"a\") ", "{ \"b\" }"),
                ("print(\"a\") ", "{ \"b\" }"),
                ("say ", "{ \"b\" }"),
            ]);
            (raw(before, rest, "takes no function", None), None)
        }
        Kind::EmptyFn => (
            raw(
                "twice ",
                "fn() { say(\"a\") }",
                "no `fn()`",
                Some("leave out `fn()`"),
            ),
            None,
        ),
        Kind::MatchCall => (
            raw(
                "match apply(\"a\") ",
                "fn(t) { t } {\n    _ { say(\"a\") }\n  }",
                "to open the blocks of the `match`",
                None,
            ),
            None,
        ),
        Kind::Effect => (
            raw("quiet()", "", "", None),
            item(
                "fn quiet() {\n  twice { ",
                "print(\"a\") }\n}",
                "needs the `console` effect, which `quiet` does not declare",
                Some("`fn quiet() ! console`"),
            ),
        ),
    }
}

/// A value as the program holds it.
#[derive(Clone)]
enum Val {
    Text(Vec<u8>),
    Cat(Vec<u8>),
    Pet(Option<Vec<u8>>),
    Unit,
    Pair(Vec<u8>, Vec<u8>),
    Fn(Closure),
}

/// A function a value holds: a lambda and the names of the function it is
/// written in, or a function with a name.
#[derive(Clone)]
enum Closure {
    Lambda(Rc<LambdaM>, Frame),
    Plain(usize),
}

type Frame = Rc<RefCell<Vec<(String, Val)>>>;

fn text_of(v: Val) -> Vec<u8> {
    match v {
        Val::Text(t) => t,
        _ => panic!("not text"),
    }
}

fn cat_name(v: Val) -> Vec<u8> {
    match v {
        Val::Cat(t) => t,
        _ => panic!("not a `Cat`"),
    }
}

fn lookup(frame: &Frame, name: &str) -> Val {
    (frame.borrow().iter().rev())
        .find(|(n, _)| n == name)
        .unwrap_or_else(|| panic!("`{name}` is defined"))
        .1
        .clone()
}

/// Runs statements, and gives the value of the last if it is an
/// expression.
fn stmts(m: &Model, frame: &Frame, list: &[Stmt], out: &mut Vec<u8>) -> Option<Val> {
    let mut last = None;
    for s in list {
        last = None;
        match s {
            Stmt::Define(name, e) | Stmt::Var(name, e) => {
                let v = eval(m, frame, e, out);
                frame.borrow_mut().push((name.clone(), v));
            }
            Stmt::Assign(name, e) => {
                let v = eval(m, frame, e, out);
                let mut names = frame.borrow_mut();
                let at = names.iter().rposition(|(n, _)| n == name).expect("defined");
                names[at].1 = v;
            }
            Stmt::Print(e) => {
                let v = text_of(eval(m, frame, e, out));
                out.extend_from_slice(&v);
                out.push(b'\n');
            }
            Stmt::Expr(e) => last = Some(eval(m, frame, e, out)),
            Stmt::Raw(..) => unreachable!("a mistake"),
        }
    }
    last
}

/// Calls a function a value holds. A lambda runs among the names of the
/// function it is written in, with its parameters added for the call.
fn call(m: &Model, f: Closure, args: Vec<Val>, out: &mut Vec<u8>) -> Val {
    match f {
        Closure::Plain(k) => plain(k, args),
        Closure::Lambda(l, frame) => {
            let before = frame.borrow().len();
            for (name, v) in l.params.iter().zip(args) {
                frame.borrow_mut().push((name.clone(), v));
            }
            let given = stmts(m, &frame, &l.body, out);
            frame.borrow_mut().truncate(before);
            given.unwrap_or(Val::Unit)
        }
    }
}

/// What a pure function with a name gives.
fn plain(k: usize, args: Vec<Val>) -> Val {
    let mut args = args.into_iter();
    let mut next = || args.next().expect("an argument");
    match PLAIN[k].0 {
        "name_of" => Val::Text(cat_name(next())),
        "cat_of" => Val::Cat(text_of(next())),
        "echo" => next(),
        "pet_of" => Val::Pet(Some(cat_name(next()))),
        "join" => {
            let a = text_of(next());
            Val::Pair(a, cat_name(next()))
        }
        "first" | "second" => {
            let Val::Pair(s, c) = next() else {
                panic!("not a pair")
            };
            if PLAIN[k].0 == "first" {
                Val::Text(s)
            } else {
                Val::Cat(c)
            }
        }
        "drop" => Val::Unit,
        other => unreachable!("{other}"),
    }
}

fn eval(m: &Model, frame: &Frame, e: &Expr, out: &mut Vec<u8>) -> Val {
    match e {
        Expr::Lit(pieces) => Val::Text(pieces.iter().flat_map(|p| PIECES[*p].1.bytes()).collect()),
        Expr::Name(name) => lookup(frame, name),
        Expr::Field(of) => Val::Text(cat_name(eval(m, frame, of, out))),
        Expr::Cat(name) => Val::Cat(text_of(eval(m, frame, name, out))),
        Expr::Pet(cat) => Val::Pet(cat.as_ref().map(|c| cat_name(eval(m, frame, c, out)))),
        Expr::Unit => Val::Unit,
        Expr::Pair(a, b) => {
            let a = text_of(eval(m, frame, a, out));
            Val::Pair(a, cat_name(eval(m, frame, b, out)))
        }
        Expr::Say(text) => {
            let text = text_of(eval(m, frame, text, out));
            out.extend_from_slice(&text);
            out.push(b'\n');
            Val::Unit
        }
        Expr::Plain(k, args) => {
            let args = args.iter().map(|a| eval(m, frame, a, out)).collect();
            plain(*k, args)
        }
        Expr::PlainValue(k) => Val::Fn(Closure::Plain(*k)),
        Expr::CallValue(name, args) => {
            let Val::Fn(f) = lookup(frame, name) else {
                panic!("`{name}` holds a function")
            };
            let args = args.iter().map(|a| eval(m, frame, a, out)).collect();
            call(m, f, args, out)
        }
        Expr::Call(h, args) => {
            // The arguments are worked out in the order they are written.
            let ho = &m.hos[*h];
            let mut given: Vec<Option<Val>> = vec![None; ho.params().len()];
            for arg in args {
                given[arg.param] = Some(eval(m, frame, &arg.value, out));
            }
            let names = (given.into_iter().enumerate())
                .map(|(i, v)| (ho.param_name(i), v.expect("given")))
                .collect();
            let inner = Rc::new(RefCell::new(names));
            stmts(m, &inner, &ho.body, out).unwrap_or(Val::Unit)
        }
        Expr::Lambda(l) => Val::Fn(Closure::Lambda(l.clone(), frame.clone())),
        Expr::Match(of, bound, cat, none) => match eval(m, frame, of, out) {
            Val::Pet(Some(c)) => {
                frame.borrow_mut().push((bound.clone(), Val::Cat(c)));
                let v = eval(m, frame, cat, out);
                let mut names = frame.borrow_mut();
                let at = names.iter().rposition(|(n, _)| n == bound).expect("bound");
                names.remove(at);
                v
            }
            Val::Pet(None) => eval(m, frame, none, out),
            _ => panic!("a `match` on what is not a `Pet`"),
        },
    }
}

/// The source being written, and the mistake once it is placed. When
/// `parens` is set, every expression is written in parentheses.
struct Writer {
    text: String,
    mistake: Option<Expected>,
    parens: bool,
    /// Whether a lambda writes the types of its parameters, and the type of
    /// its result.
    typed: (bool, bool),
}

fn type_text(ty: &Ty) -> &'static str {
    match ty {
        Ty::Str => "str",
        Ty::Cat => "Cat",
        Ty::Pet => "Pet",
        Ty::Unit => "()",
        Ty::Pair => "(str, Cat)",
        Ty::T => "T",
    }
}

fn fn_text(f: &FnTy) -> String {
    let params: Vec<&str> = f.params.iter().map(type_text).collect();
    match f.result {
        Ty::Unit => format!("fn({})", params.join(", ")),
        _ => format!("fn({}) -> {}", params.join(", "), type_text(&f.result)),
    }
}

impl Writer {
    fn push(&mut self, s: &str) {
        self.text.push_str(s);
    }

    fn mark(&mut self, mentions: &str, fix: Option<String>) {
        assert!(self.mistake.is_none(), "one mistake per file");
        self.mistake = Some(Expected {
            at: pos_after(&self.text),
            mentions: mentions.to_string(),
            fix,
        });
    }

    fn list(&mut self, items: &[Expr], indent: usize) {
        for (i, item) in items.iter().enumerate() {
            if i > 0 {
                self.push(", ");
            }
            self.expr(item, indent);
        }
    }

    fn expr(&mut self, e: &Expr, indent: usize) {
        if self.parens {
            self.push("(");
        }
        self.expr_inner(e, indent);
        if self.parens {
            self.push(")");
        }
    }

    fn expr_inner(&mut self, e: &Expr, indent: usize) {
        match e {
            Expr::Lit(pieces) => {
                self.push("\"");
                for p in pieces {
                    self.push(PIECES[*p].0);
                }
                self.push("\"");
            }
            Expr::Name(name) => self.push(name),
            Expr::Field(of) => {
                self.expr(of, indent);
                self.push(".name");
            }
            Expr::Cat(name) => {
                self.push("Cat(name: ");
                self.expr(name, indent);
                self.push(")");
            }
            Expr::Pet(None) => self.push("Pet.none"),
            Expr::Pet(Some(c)) => {
                self.push("Pet.cat(");
                self.expr(c, indent);
                self.push(")");
            }
            Expr::Unit => self.push("()"),
            Expr::Pair(a, b) => {
                self.push("(");
                self.expr(a, indent);
                self.push(", ");
                self.expr(b, indent);
                self.push(")");
            }
            Expr::Say(text) => {
                self.push("say(");
                self.expr(text, indent);
                self.push(")");
            }
            Expr::Plain(k, args) => {
                self.push(PLAIN[*k].0);
                self.push("(");
                self.list(args, indent);
                self.push(")");
            }
            Expr::PlainValue(k) => self.push(PLAIN[*k].0),
            Expr::CallValue(name, args) => {
                self.push(name);
                self.push("(");
                self.list(args, indent);
                self.push(")");
            }
            Expr::Call(h, args) => self.call(*h, args, indent),
            Expr::Lambda(l) => self.lambda(l, indent),
            Expr::Match(of, bound, cat, none) => {
                let pad = "  ".repeat(indent);
                self.push("match ");
                // After the value of a `match`, `{` opens its blocks, so a
                // call with a lambda after it is written in parentheses.
                let after = matches!(&**of, Expr::Call(_, args) if args.iter().any(|a| a.how == How::After));
                if after && !self.parens {
                    self.push("(");
                    self.expr(of, indent);
                    self.push(")");
                } else {
                    self.expr(of, indent);
                }
                self.push(&format!(" {{\n{pad}  .cat({bound}) {{ "));
                self.expr(cat, indent + 1);
                self.push(&format!(" }}\n{pad}  .none {{ "));
                self.expr(none, indent + 1);
                self.push(&format!(" }}\n{pad}}}"));
            }
        }
    }

    fn call(&mut self, h: usize, args: &[Arg], indent: usize) {
        self.push(&format!("h{h}"));
        let (after, inside): (Vec<&Arg>, Vec<&Arg>) =
            args.iter().partition(|a| a.how == How::After);
        // A call whose only argument is a lambda after it has no
        // parentheses.
        if !inside.is_empty() || after.is_empty() {
            self.push("(");
            for (i, arg) in inside.iter().enumerate() {
                if i > 0 {
                    self.push(", ");
                }
                if arg.how == How::Named {
                    self.push(&format!("{}: ", arg.name));
                }
                self.expr(&arg.value, indent);
            }
            self.push(")");
        }
        for arg in after {
            let Expr::Lambda(l) = &arg.value else {
                unreachable!("a lambda")
            };
            self.push(" ");
            self.lambda(l, indent);
        }
    }

    fn lambda(&mut self, l: &LambdaM, indent: usize) {
        if !l.params.is_empty() {
            let params: Vec<String> = (l.params.iter().zip(&l.types))
                .map(|(name, ty)| match self.typed.0 {
                    true => format!("{name}: {}", type_text(ty)),
                    false => name.clone(),
                })
                .collect();
            let mut head = format!("fn({})", params.join(", "));
            if self.typed.1 && l.result != Ty::Unit {
                head += &format!(" -> {}", type_text(&l.result));
            }
            self.push(&format!("{head} "));
        }
        match l.body.as_slice() {
            [] => self.push("{}"),
            [Stmt::Expr(e)] => {
                self.push("{ ");
                self.expr(e, indent + 1);
                self.push(" }");
            }
            body => {
                self.push("{\n");
                self.stmts(body, indent + 1);
                self.push(&"  ".repeat(indent));
                self.push("}");
            }
        }
    }

    fn stmts(&mut self, list: &[Stmt], indent: usize) {
        let pad = "  ".repeat(indent);
        for s in list {
            self.push(&pad);
            match s {
                Stmt::Define(name, e) => {
                    self.push(&format!("{name} = "));
                    self.expr(e, indent);
                }
                Stmt::Var(name, e) => {
                    self.push(&format!("var {name} = "));
                    self.expr(e, indent);
                }
                Stmt::Assign(name, e) => {
                    self.push(&format!("{name} = "));
                    self.expr(e, indent);
                }
                Stmt::Print(e) => {
                    self.push("print(");
                    self.expr(e, indent);
                    self.push(")");
                }
                Stmt::Expr(e) => self.expr(e, indent),
                Stmt::Raw(before, rest, mentions, fix) => {
                    self.push(before);
                    if !mentions.is_empty() {
                        self.mark(mentions, fix.clone());
                    }
                    self.push(rest);
                }
            }
            self.push("\n");
        }
    }

    fn program(&mut self, m: &Model) {
        self.push("use std.console (console, print)\n\n");
        self.push("struct Cat {\n  name: str\n}\n\nenum Pet {\n  cat: Cat\n  none\n}\n");
        self.push("\nfn say(text: str) ! console { print(text) }\n");
        for (name, params, result, body) in PLAIN {
            let params: Vec<String> = (params.iter().enumerate())
                .map(|(i, t)| format!("a{i}: {}", type_text(t)))
                .collect();
            self.push(&format!("\nfn {name}({})", params.join(", ")));
            if *result != Ty::Unit {
                self.push(&format!(" -> {}", type_text(result)));
            }
            self.push(&format!(" {{ {body} }}\n"));
        }
        self.push("\nfn twice(body: fn()) {\n  body()\n  body()\n}\n");
        self.push("\nfn apply(text: str, f: fn(str) -> str) -> str { f(text) }\n");
        self.push("\nfn main() ! console {\n");
        self.stmts(&m.main, 1);
        self.push("}\n");
        for (h, ho) in m.hos.iter().enumerate() {
            self.push(&format!("\nfn h{h}"));
            if ho.generic {
                self.push("[T]");
            }
            let params: Vec<String> = (ho.params().iter().enumerate())
                .map(|(i, p)| {
                    let ty = match p {
                        Ok(t) => type_text(t).to_string(),
                        Err(f) => fn_text(f),
                    };
                    format!("{}: {ty}", ho.param_name(i))
                })
                .collect();
            self.push(&format!("({})", params.join(", ")));
            if ho.result != Ty::Unit {
                self.push(&format!(" -> {}", type_text(&ho.result)));
            }
            if ho.console {
                self.push(" ! console");
            }
            // Nothing is known of `T` but what its bounds say.
            if ho.generic {
                self.push("\n  requires Copy[T]\n  requires Discard[T]\n{\n");
            } else {
                self.push(" {\n");
            }
            self.stmts(&ho.body, 1);
            self.push("}\n");
        }
        if let Some(Item(before, rest, mentions, fix)) = &m.item {
            self.push("\n");
            self.push(before);
            self.mark(mentions, fix.clone());
            self.push(rest);
            self.push("\n");
        }
    }
}

fn build(g: &mut Gen, kind: Option<Kind>) -> LambdaCase {
    let mut m = model(g);
    if let Some(kind) = kind {
        let (stmt, item) = mistake(g, kind);
        m.main.push(stmt);
        m.item = item;
    }
    m.main.push(Stmt::Print(Expr::Name("note".to_string())));
    let mut stdout = Vec::new();
    if kind.is_none() {
        let frame = Rc::new(RefCell::new(Vec::new()));
        stmts(&m, &frame, &m.main, &mut stdout);
    }
    let mut w = Writer {
        text: String::new(),
        mistake: None,
        parens: false,
        typed: (false, false),
    };
    w.program(&m);
    let mut parenthesized = Writer {
        text: String::new(),
        mistake: None,
        parens: true,
        typed: (false, false),
    };
    let mut typed = Vec::new();
    if kind.is_none() {
        parenthesized.program(&m);
        for typing in [(true, false), (false, true), (true, true)] {
            let mut w = Writer {
                text: String::new(),
                mistake: None,
                parens: false,
                typed: typing,
            };
            w.program(&m);
            typed.push(w.text);
        }
    }
    assert_eq!(
        w.mistake.is_some(),
        kind.is_some(),
        "{kind:?} in {}",
        w.text
    );
    LambdaCase {
        source: w.text,
        parenthesized: parenthesized.text,
        typed,
        stdout,
        // `main`, `say`, and each function with a name.
        copies: 2 + PLAIN.len(),
        mistake: w.mistake,
    }
}

// A function that calls its lambda in loops, and the `var`s the lambda
// assigns.

/// How a function calls the lambda it is given, `body`, which takes a
/// number and gives nothing.
#[derive(Clone, Debug)]
enum Shape {
    /// `body(x)`, with a number written out, or the counter of the
    /// `loop` around it that is `depth` rounds in, counting from the
    /// outermost.
    Call(Option<usize>, u32),
    Seq(Vec<Shape>),
    /// `loop i in 0..n { ... }`.
    Range(u32, Box<Shape>),
    /// An open-ended `loop` that breaks after `n` rounds.
    Open(u32, Box<Shape>),
    /// `if n == k { ... } else { ... }` on the first parameter.
    If(u32, Box<Shape>, Box<Shape>),
    /// A `match` on the second parameter.
    Match(Box<Shape>, Box<Shape>),
    /// A call of the function `f` with an earlier place, passing on the
    /// lambda, with the two values it takes.
    Pass(usize, u32, bool),
}

/// What the lambda does with the `var`s `a`, `b` and the field `p.a`, for
/// the number `x` it is given. Each is a `u32` that wraps.
#[derive(Clone, Copy, Debug)]
enum Effect {
    /// `a = wrapping_add(a, x)`
    Add,
    /// `a = wrapping_add(wrapping_mul(a, 3), x)`, which reads what it assigns.
    ReadsAndAssigns,
    /// `a` and `b` both.
    Two,
    /// `p.a = wrapping_add(p.a, x)`, a field of a `var`.
    Field,
}

const EFFECTS: &[Effect] = &[
    Effect::Add,
    Effect::ReadsAndAssigns,
    Effect::Two,
    Effect::Field,
];

#[derive(Clone, Copy, Default)]
struct Vars {
    a: u32,
    b: u32,
    p: u32,
}

impl Effect {
    fn apply(self, v: &mut Vars, x: u32) {
        match self {
            Effect::Add => v.a = v.a.wrapping_add(x),
            Effect::ReadsAndAssigns => v.a = v.a.wrapping_mul(3).wrapping_add(x),
            Effect::Two => {
                v.a = v.a.wrapping_add(x);
                v.b = v.b.wrapping_mul(2).wrapping_add(1);
            }
            Effect::Field => v.p = v.p.wrapping_add(x),
        }
    }

    fn text(self) -> &'static str {
        match self {
            Effect::Add => "a = wrapping_add(a, x)",
            Effect::ReadsAndAssigns => "a = wrapping_add(wrapping_mul(a, 3), x)",
            Effect::Two => "a = wrapping_add(a, x)\n    b = wrapping_add(wrapping_mul(b, 2), 1)",
            Effect::Field => "p.a = wrapping_add(p.a, x)",
        }
    }
}

/// What a function does when called with `n` and the case of `Mode` that
/// `second` says, `f` being the lambda and `others` the functions before it.
fn run_shape(
    shape: &Shape,
    n: u32,
    second: bool,
    counters: &mut Vec<u32>,
    others: &[Shape],
    v: &mut Vars,
    effect: Effect,
) {
    match shape {
        Shape::Call(counter, k) => {
            let x = counter.map_or(*k, |d| counters[d]);
            effect.apply(v, x);
        }
        Shape::Seq(items) => {
            for item in items {
                run_shape(item, n, second, counters, others, v, effect);
            }
        }
        Shape::Range(rounds, inner) => {
            counters.push(0);
            let depth = counters.len() - 1;
            for i in 0..*rounds {
                counters[depth] = i;
                run_shape(inner, n, second, counters, others, v, effect);
            }
            counters.pop();
        }
        Shape::Open(rounds, inner) => {
            // Its counter is not offered to a call, so a call inside holds
            // the depth of the nearest `loop` over a range.
            for _ in 0..*rounds {
                run_shape(inner, n, second, counters, others, v, effect);
            }
        }
        Shape::If(k, then, otherwise) => {
            let chosen = if n == *k { then } else { otherwise };
            run_shape(chosen, n, second, counters, others, v, effect);
        }
        Shape::Match(one, two) => {
            let chosen = if second { two } else { one };
            run_shape(chosen, n, second, counters, others, v, effect);
        }
        Shape::Pass(f, n, second) => {
            let mut inner = Vec::new();
            run_shape(
                &others[*f],
                *n,
                *second,
                &mut inner,
                &others[..*f],
                v,
                effect,
            );
        }
    }
}

/// Whether the function runs an open-ended loop, or calls one that does.
fn diverges(shape: &Shape, others: &[Shape]) -> bool {
    match shape {
        Shape::Call(..) => false,
        Shape::Seq(items) => items.iter().any(|s| diverges(s, others)),
        Shape::Range(_, s) => diverges(s, others),
        Shape::Open(..) => true,
        Shape::If(_, a, b) | Shape::Match(a, b) => diverges(a, others) || diverges(b, others),
        Shape::Pass(f, ..) => diverges(&others[*f], &others[..*f]),
    }
}

/// A shape of at most `size` levels, `ranges` loops over a range being around
/// it, so that a call may read the counter of one, and `before` functions
/// being there to pass the lambda on to.
fn shape_of(g: &mut Gen, size: usize, ranges: usize, before: usize) -> Shape {
    let choices = if size == 0 { 1 } else { 7 };
    match g.below(choices) {
        0 => {
            let counter = (ranges > 0 && g.one_in(2)).then(|| g.below(ranges));
            Shape::Call(counter, g.below(5) as u32 + 1)
        }
        1 => {
            let items = g.below(2) + 2;
            Shape::Seq(
                (0..items)
                    .map(|_| shape_of(g, size - 1, ranges, before))
                    .collect(),
            )
        }
        2 => Shape::Range(
            g.below(4) as u32,
            Box::new(shape_of(g, size - 1, ranges + 1, before)),
        ),
        3 => Shape::Open(
            g.below(4) as u32,
            Box::new(shape_of(g, size - 1, ranges, before)),
        ),
        4 => Shape::If(
            g.below(2) as u32,
            Box::new(shape_of(g, size - 1, ranges, before)),
            Box::new(shape_of(g, size - 1, ranges, before)),
        ),
        5 => Shape::Match(
            Box::new(shape_of(g, size - 1, ranges, before)),
            Box::new(shape_of(g, size - 1, ranges, before)),
        ),
        _ if before > 0 => Shape::Pass(g.below(before), g.below(2) as u32, g.one_in(2)),
        _ => Shape::Call(None, 1),
    }
}

fn indent_of(depth: usize) -> String {
    "  ".repeat(depth)
}

/// The text of a shape, which `counters` are the names of the counters of
/// the loops around it and `opens` how many open-ended loops are around it.
fn write_shape(
    shape: &Shape,
    depth: usize,
    counters: &mut Vec<String>,
    opens: &mut usize,
    out: &mut String,
) {
    let pad = indent_of(depth);
    match shape {
        Shape::Call(counter, k) => {
            let x = counter.map_or(k.to_string(), |d| counters[d].clone());
            out.push_str(&format!("{pad}body({x})\n"));
        }
        Shape::Seq(items) => {
            for item in items {
                write_shape(item, depth, counters, opens, out);
            }
        }
        Shape::Range(rounds, inner) => {
            let name = format!("i{}", counters.len());
            out.push_str(&format!("{pad}loop {name} in 0..{rounds} {{\n"));
            counters.push(name);
            write_shape(inner, depth + 1, counters, opens, out);
            counters.pop();
            out.push_str(&format!("{pad}}}\n"));
        }
        Shape::Open(rounds, inner) => {
            let name = format!("k{}", *opens);
            *opens += 1;
            out.push_str(&format!("{pad}var {name}: u32 = 0\n{pad}loop {{\n"));
            out.push_str(&format!("{pad}  if {name} == {rounds} {{ break }}\n"));
            write_shape(inner, depth + 1, counters, opens, out);
            out.push_str(&format!(
                "{pad}  {name} = wrapping_add({name}, 1)\n{pad}}}\n"
            ));
        }
        Shape::If(k, then, otherwise) => {
            out.push_str(&format!("{pad}if n == {k} {{\n"));
            write_shape(then, depth + 1, counters, opens, out);
            out.push_str(&format!("{pad}}} else {{\n"));
            write_shape(otherwise, depth + 1, counters, opens, out);
            out.push_str(&format!("{pad}}}\n"));
        }
        Shape::Match(one, two) => {
            out.push_str(&format!("{pad}match m {{\n{pad}  .one {{\n"));
            write_shape(one, depth + 2, counters, opens, out);
            out.push_str(&format!("{pad}  }}\n{pad}  .two {{\n"));
            write_shape(two, depth + 2, counters, opens, out);
            out.push_str(&format!("{pad}  }}\n{pad}}}\n"));
        }
        Shape::Pass(f, n, second) => {
            let mode = if *second { ".two" } else { ".one" };
            out.push_str(&format!("{pad}f{f}({n}, {mode}, body)\n"));
        }
    }
}

/// A program in which functions call the lambda they are given in loops,
/// and what it prints.
#[derive(Clone)]
pub struct LoopCase {
    pub source: String,
    pub stdout: Vec<u8>,
}

impl std::fmt::Debug for LoopCase {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self.source)
    }
}

/// A program with functions that call their lambda in a `loop` over a
/// range, in an open-ended loop, in nested loops, under an `if` or a
/// `match`, or by passing it on, and a `main` that gives one a lambda
/// that assigns `var`s. What it prints is what the lambda's statements
/// written in place of each call give.
pub fn loop_program(g: &mut Gen) -> LoopCase {
    let count = g.below(3) + 1;
    let mut shapes: Vec<Shape> = Vec::new();
    for at in 0..count {
        let shape = shape_of(g, 3, 0, at);
        shapes.push(shape);
    }
    let effect = *g.pick(EFFECTS);
    let called = g.below(count);
    let (n, second) = (g.below(2) as u32, g.one_in(2));

    let mut v = Vars::default();
    run_shape(
        &shapes[called],
        n,
        second,
        &mut Vec::new(),
        &shapes[..called],
        &mut v,
        effect,
    );

    let mut text =
        String::from("struct Pair {\n  a: u32\n  b: u32\n}\n\nenum Mode {\n  one\n  two\n}\n");
    for (at, shape) in shapes.iter().enumerate() {
        let diverge = if diverges(shape, &shapes[..at]) {
            " ! diverge"
        } else {
            ""
        };
        text.push_str(&format!(
            "\nfn f{at}(n: u32, m: Mode, body: fn(u32)){diverge} {{\n"
        ));
        write_shape(shape, 1, &mut Vec::new(), &mut 0, &mut text);
        text.push_str("}\n");
    }
    let diverge = if diverges(&shapes[called], &shapes[..called]) {
        "diverge, "
    } else {
        ""
    };
    let mode = if second { ".two" } else { ".one" };
    text.push_str(&format!(
        "\nfn main() ! {diverge}console, alloc {{\n  var a: u32 = 0\n  var b: u32 = 0\n  var p = Pair(a: 0, b: 0)\n  f{called}({n}, {mode}) fn(x) {{\n    {}\n  }}\n  print(\"{{a}} {{b}} {{p.a}}\")\n}}\n",
        effect.text()
    ));
    // `wrapping_mul` is written by only some of the effects.
    let int = if text.contains("wrapping_mul(") {
        "wrapping_add, wrapping_mul"
    } else {
        "wrapping_add"
    };
    let text = format!(
        "use std.bits (u32)\nuse std.console (console, print)\nuse std.int ({int})\n\n{text}"
    );
    LoopCase {
        source: text,
        stdout: format!("{} {} {}\n", v.a, v.b, v.p).into_bytes(),
    }
}
