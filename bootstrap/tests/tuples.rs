//! The generator of programs with tuples, which can also place one mistake
//! in what it builds.
//!
//! A program has a struct `Cat`, an enum `Pet` one of whose cases holds a
//! tuple, `say`, which prints and has no return type, and `swap`, which has
//! type parameters. Its tuples hold text, a `Cat`, a `Pet`, `()`, and other
//! tuples. A few functions each take a tuple, take it apart, and return a
//! value built from its parts, and `main` calls each of them. Values are
//! built, bound, passed, and returned, and taken apart by patterns in
//! bindings, where a name may be marked `var` or be a `var` already, which
//! the pattern assigns to, and in `match`, used as a statement and as an
//! expression. Calls of `say` inside tuples show the order in which their
//! elements are worked out.
//!
//! The generator first builds a model of the program and works out from it
//! what the program prints. Then it writes the model out, placing the
//! mistake as it writes.

use crate::generate::Gen;
use crate::programs::{Expected, PIECES, pos_after};

/// A program with tuples: its source, what it prints, and the mistake
/// placed in it, if any.
pub struct TupleCase {
    pub source: String,
    pub stdout: Vec<u8>,
    pub mistake: Option<Expected>,
}

impl std::fmt::Debug for TupleCase {
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
    Tup(Vec<Ty>),
}

/// The cases of `Pet`, each with the type it holds, if it holds one.
fn cases() -> Vec<(&'static str, Option<Ty>)> {
    vec![
        ("cat", Some(Ty::Cat)),
        ("pair", Some(Ty::Tup(vec![Ty::Str, Ty::Cat]))),
        ("none", None),
    ]
}

/// A pattern: a name it defines, which is a `var` if marked so, a `var` it
/// assigns to, a case of `Pet` by its place, or a tuple of patterns.
#[derive(Clone, Debug)]
enum Pat {
    Bind(String, bool),
    Assign(String),
    Case(usize, Option<Box<Pat>>),
    Tuple(Vec<Pat>),
}

#[derive(Clone, Debug)]
struct Arm {
    pat: Pat,
    stmts: Vec<Stmt>,
}

#[derive(Clone, Debug)]
enum Expr {
    /// A literal, as the places of its pieces in `PIECES`.
    Lit(Vec<usize>),
    Name(String),
    /// The `name` of a `Cat`.
    Field(Box<Expr>),
    Cat(Box<Expr>),
    /// `()`.
    Unit,
    Say(Box<Expr>),
    /// A case of `Pet`, written in full, with what it holds.
    Case(usize, Option<Box<Expr>>),
    Tuple(Vec<Expr>),
    /// A call of function `f`.
    Call(usize, Box<Expr>),
    Swap(Box<Expr>),
    Match(Box<Expr>, Vec<Arm>),
}

#[derive(Clone, Debug)]
enum Stmt {
    Define(String, Option<Ty>, Expr),
    Var(String, Expr),
    Assign(String, Expr),
    Destructure(Pat, Option<Ty>, Expr),
    Print(Expr),
    Expr(Expr),
    /// Lines written as they are, with the mistake placed after the first
    /// part: what it mentions and what its fix mentions, if it has one.
    Raw(String, String, String, Option<String>),
}

struct FnDef {
    param: Ty,
    result: Ty,
    /// Whether `-> ()` is written, for a function that returns `()`.
    written: bool,
    body: Vec<Stmt>,
}

struct Model {
    fns: Vec<FnDef>,
    main: Vec<Stmt>,
}

/// The kinds of mistake the generator can place, each once in a file.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Kind {
    Shape,
    NotAVar,
    VarHolds,
    Twice,
    TupleWhereText,
    LeftOut,
    NeverReached,
    NotBuilt,
}

const KINDS: &[Kind] = &[
    Kind::Shape,
    Kind::NotAVar,
    Kind::VarHolds,
    Kind::Twice,
    Kind::TupleWhereText,
    Kind::LeftOut,
    Kind::NeverReached,
    Kind::NotBuilt,
];

const NOT_BUILT: &str = "not supported yet";

/// A valid program.
pub fn program(g: &mut Gen) -> TupleCase {
    build(g, None)
}

/// A program with one mistake in it.
pub fn program_with_mistake(g: &mut Gen) -> TupleCase {
    let kind = *g.pick(KINDS);
    build(g, Some(kind))
}

/// A type, with tuples nested while `depth` allows.
fn some_type(g: &mut Gen, depth: usize) -> Ty {
    match g.below(6) {
        0 => Ty::Str,
        1 => Ty::Cat,
        2 => Ty::Pet,
        3 => Ty::Unit,
        _ if depth > 0 => tuple_type(g, depth),
        _ => Ty::Str,
    }
}

fn tuple_type(g: &mut Gen, depth: usize) -> Ty {
    Ty::Tup(
        (0..2 + g.below(2))
            .map(|_| some_type(g, depth - 1))
            .collect(),
    )
}

/// What generating code needs to know: the names defined and the `var`s
/// among them, the functions it may call, and a count for fresh names.
#[derive(Clone)]
struct Ctx {
    names: Vec<(String, Ty)>,
    vars: Vec<String>,
    callable: std::ops::Range<usize>,
    fresh: usize,
}

impl Ctx {
    fn fresh(&mut self, prefix: &str) -> String {
        self.fresh += 1;
        format!("{prefix}{}", self.fresh)
    }

    fn named(&self, ty: &Ty) -> Vec<String> {
        (self.names.iter())
            .filter(|(_, t)| t == ty)
            .map(|(n, _)| n.clone())
            .collect()
    }

    /// The names a `match` can take apart: of a `Pet`, or of a tuple.
    fn matchable(&self) -> Vec<(String, Ty)> {
        (self.names.iter())
            .filter(|(_, t)| matches!(t, Ty::Pet | Ty::Tup(_)))
            .cloned()
            .collect()
    }

    /// Defines the names a pattern binds, when it matches a value of `ty`.
    fn bind(&mut self, pat: &Pat, ty: &Ty) {
        match pat {
            Pat::Bind(name, var) => {
                self.names.push((name.clone(), ty.clone()));
                if *var {
                    self.vars.push(name.clone());
                }
            }
            Pat::Assign(_) => {}
            Pat::Case(k, Some(inner)) => {
                let holds = cases()[*k].1.clone().expect("holds a value");
                self.bind(inner, &holds);
            }
            Pat::Case(..) => {}
            Pat::Tuple(items) => {
                let Ty::Tup(of) = ty else {
                    unreachable!("a tuple")
                };
                for (item, t) in items.iter().zip(of) {
                    self.bind(item, t);
                }
            }
        }
    }
}

struct Writing<'a> {
    g: &'a mut Gen,
    m: &'a Model,
}

impl Writing<'_> {
    fn literal(&mut self) -> Expr {
        let g = &mut *self.g;
        Expr::Lit((0..g.below(3)).map(|_| g.below(PIECES.len())).collect())
    }

    /// A call of a function that `ctx` allows that returns `ty`, if any.
    fn call(&mut self, ctx: &mut Ctx, ty: &Ty, depth: usize) -> Option<Expr> {
        let fns: Vec<usize> = (ctx.callable.clone())
            .filter(|f| self.m.fns[*f].result == *ty)
            .collect();
        if fns.is_empty() || depth == 0 {
            return None;
        }
        let f = *self.g.pick(&fns);
        let param = self.m.fns[f].param.clone();
        let arg = self.value(ctx, &param, depth - 1);
        Some(Expr::Call(f, Box::new(arg)))
    }

    /// An expression that gives text.
    fn text(&mut self, ctx: &mut Ctx, depth: usize) -> Expr {
        let texts = ctx.named(&Ty::Str);
        let cats = ctx.named(&Ty::Cat);
        let matchable = ctx.matchable();
        match self.g.below(6) {
            1 if !texts.is_empty() => Expr::Name(self.g.pick(&texts).clone()),
            2 if !cats.is_empty() => Expr::Field(Box::new(Expr::Name(self.g.pick(&cats).clone()))),
            3 => self
                .call(ctx, &Ty::Str, depth)
                .unwrap_or_else(|| self.literal()),
            4 if depth > 0 && !matchable.is_empty() => {
                let (name, ty) = self.g.pick(&matchable).clone();
                let arms = self.arms(ctx, &ty, depth - 1, true);
                Expr::Match(Box::new(Expr::Name(name)), arms)
            }
            _ => self.literal(),
        }
    }

    /// An expression of the type.
    fn value(&mut self, ctx: &mut Ctx, ty: &Ty, depth: usize) -> Expr {
        let named = ctx.named(ty);
        if !named.is_empty() && self.g.one_in(3) {
            return Expr::Name(self.g.pick(&named).clone());
        }
        let inner = depth.saturating_sub(1);
        match ty {
            Ty::Str => self.text(ctx, inner),
            Ty::Cat => Expr::Cat(Box::new(self.text(ctx, inner))),
            Ty::Unit => match self.g.below(3) {
                0 => Expr::Unit,
                1 => self
                    .call(ctx, ty, depth)
                    .unwrap_or_else(|| Expr::Say(Box::new(self.literal()))),
                _ => Expr::Say(Box::new(self.text(ctx, inner))),
            },
            Ty::Pet => {
                let k = self.g.below(3);
                let holds = cases()[k].1.clone();
                let payload = holds.map(|t| Box::new(self.value(ctx, &t, inner)));
                Expr::Case(k, payload)
            }
            Ty::Tup(items) => match self.g.below(4) {
                0 => self
                    .call(ctx, ty, depth)
                    .unwrap_or_else(|| self.tuple(ctx, items, inner)),
                1 if items.len() == 2 => {
                    let swapped = Ty::Tup(vec![items[1].clone(), items[0].clone()]);
                    Expr::Swap(Box::new(self.value(ctx, &swapped, inner)))
                }
                _ => self.tuple(ctx, items, inner),
            },
        }
    }

    fn tuple(&mut self, ctx: &mut Ctx, items: &[Ty], depth: usize) -> Expr {
        Expr::Tuple(items.iter().map(|t| self.value(ctx, t, depth)).collect())
    }

    /// A pattern for a binding of a value of the type, which is a tuple
    /// when `tuple`: of patterns while `depth` allows, of `var`s already
    /// defined it assigns to, and of new names, some marked `var`. `used`
    /// holds the `var`s already assigned to in the pattern.
    fn binding(
        &mut self,
        ctx: &mut Ctx,
        ty: &Ty,
        depth: usize,
        tuple: bool,
        used: &mut Vec<String>,
    ) -> Pat {
        if let Ty::Tup(items) = ty
            && (tuple || (depth > 0 && self.g.one_in(2)))
        {
            let items = (items.iter())
                .map(|t| self.binding(ctx, t, depth.saturating_sub(1), false, used))
                .collect();
            return Pat::Tuple(items);
        }
        let vars: Vec<String> = (ctx.named(ty).into_iter())
            .filter(|n| ctx.vars.contains(n) && !used.contains(n))
            .collect();
        if !vars.is_empty() && self.g.one_in(2) {
            let name = self.g.pick(&vars).clone();
            used.push(name.clone());
            return Pat::Assign(name);
        }
        Pat::Bind(ctx.fresh("b"), self.g.one_in(3))
    }

    /// Patterns that cover every value of the type once, with none that is
    /// never reached. For a `Pet`, some of its cases, in any order, each
    /// with patterns that cover what it holds, and then a bare name if a
    /// case is left out. For a tuple, the patterns of one of its elements,
    /// with a bare name for each other. Past `depth`, or for any other
    /// type, a bare name alone.
    fn rows(&mut self, ctx: &mut Ctx, ty: &Ty, depth: usize) -> Vec<Pat> {
        let name = |w: &mut Self, ctx: &mut Ctx| Pat::Bind(ctx.fresh("a"), w.g.one_in(4));
        if depth == 0 {
            return vec![name(self, ctx)];
        }
        match ty {
            Ty::Pet => {
                let all = cases();
                let mut order: Vec<usize> = (0..all.len()).collect();
                let mut chosen = Vec::new();
                for _ in 0..all.len() {
                    if self.g.one_in(4) {
                        break;
                    }
                    chosen.push(order.remove(self.g.below(order.len())));
                }
                let mut out = Vec::new();
                for &k in &chosen {
                    match &all[k].1 {
                        None => out.push(Pat::Case(k, None)),
                        Some(t) => {
                            for inner in self.rows(ctx, t, depth - 1) {
                                out.push(Pat::Case(k, Some(Box::new(inner))));
                            }
                        }
                    }
                }
                if chosen.len() < all.len() {
                    out.push(name(self, ctx));
                }
                out
            }
            Ty::Tup(items) if !self.g.one_in(4) => {
                // An element that can be taken apart, where there is one.
                let apart: Vec<usize> = (0..items.len())
                    .filter(|i| matches!(items[*i], Ty::Pet | Ty::Tup(_)))
                    .collect();
                let at = match apart.as_slice() {
                    [] => self.g.below(items.len()),
                    apart => *self.g.pick(apart),
                };
                let inner = self.rows(ctx, &items[at], depth - 1);
                (inner.into_iter())
                    .map(|p| {
                        let items = (0..items.len())
                            .map(|i| if i == at { p.clone() } else { name(self, ctx) })
                            .collect();
                        Pat::Tuple(items)
                    })
                    .collect()
            }
            _ => vec![name(self, ctx)],
        }
    }

    /// The blocks of a `match` on a value of the type. Each gives text if
    /// `gives`, and otherwise prints, or assigns to a `var` that holds
    /// text.
    fn arms(&mut self, ctx: &mut Ctx, ty: &Ty, depth: usize, gives: bool) -> Vec<Arm> {
        let pats = self.rows(ctx, ty, 2);
        let mut out = Vec::new();
        for pat in pats {
            let mut inner = ctx.clone();
            inner.bind(&pat, ty);
            let mut stmts = Vec::new();
            if self.g.one_in(3) {
                let name = inner.fresh("t");
                let e = self.text(&mut inner, depth);
                stmts.push(Stmt::Define(name.clone(), None, e));
                inner.names.push((name, Ty::Str));
            }
            let last = self.text(&mut inner, depth);
            let vars: Vec<String> = (inner.named(&Ty::Str).into_iter())
                .filter(|n| inner.vars.contains(n))
                .collect();
            stmts.push(if gives {
                Stmt::Expr(last)
            } else if !vars.is_empty() && self.g.one_in(2) {
                Stmt::Assign(self.g.pick(&vars).clone(), last)
            } else {
                Stmt::Print(last)
            });
            ctx.fresh = inner.fresh;
            out.push(Arm { pat, stmts });
        }
        out
    }

    /// A statement, with what it defines added to `ctx`.
    fn statement(&mut self, ctx: &mut Ctx, depth: usize) -> Stmt {
        let matchable = ctx.matchable();
        let tuples: Vec<Ty> = (ctx.names.iter())
            .filter(|(_, t)| matches!(t, Ty::Tup(_)))
            .map(|(_, t)| t.clone())
            .collect();
        match self.g.below(8) {
            0 => Stmt::Print(self.text(ctx, depth)),
            1 if !ctx.vars.is_empty() => {
                let name = self.g.pick(&ctx.vars).clone();
                let ty = ctx.names.iter().find(|(n, _)| *n == name).expect("defined");
                let ty = ty.1.clone();
                Stmt::Assign(name, self.value(ctx, &ty, depth))
            }
            2 if !matchable.is_empty() => {
                let (name, ty) = self.g.pick(&matchable).clone();
                let arms = self.arms(ctx, &ty, depth.saturating_sub(1), false);
                Stmt::Expr(Expr::Match(Box::new(Expr::Name(name)), arms))
            }
            3 => {
                let ty = Ty::Tup(vec![Ty::Unit, Ty::Unit]);
                Stmt::Expr(self.value(ctx, &ty, depth))
            }
            4 => {
                let ty = some_type(self.g, 2);
                let value = self.value(ctx, &ty, depth);
                let name = ctx.fresh("v");
                ctx.names.push((name.clone(), ty));
                ctx.vars.push(name.clone());
                Stmt::Var(name, value)
            }
            5 => {
                let ty = some_type(self.g, 2);
                let value = self.value(ctx, &ty, depth);
                let name = ctx.fresh("v");
                let typed = self.g.one_in(2).then(|| ty.clone());
                ctx.names.push((name.clone(), ty));
                Stmt::Define(name, typed, value)
            }
            _ => {
                let ty = if !tuples.is_empty() && self.g.one_in(2) {
                    self.g.pick(&tuples).clone()
                } else {
                    tuple_type(self.g, 2)
                };
                let value = self.value(ctx, &ty, depth);
                let pat = self.binding(ctx, &ty, 2, true, &mut Vec::new());
                ctx.bind(&pat, &ty);
                let typed = self.g.one_in(3).then(|| ty.clone());
                Stmt::Destructure(pat, typed, value)
            }
        }
    }
}

fn model(g: &mut Gen, kind: Option<Kind>) -> Model {
    let count = 1 + g.below(3);
    let fns = (0..count)
        .map(|_| {
            let result = some_type(g, 1);
            FnDef {
                param: tuple_type(g, 2),
                written: result != Ty::Unit || g.one_in(2),
                result,
                body: Vec::new(),
            }
        })
        .collect();
    let mut m = Model {
        fns,
        main: Vec::new(),
    };
    for f in 0..count {
        let param = m.fns[f].param.clone();
        let result = m.fns[f].result.clone();
        let mut ctx = Ctx {
            names: vec![("p".to_string(), param.clone())],
            vars: Vec::new(),
            callable: f + 1..count,
            fresh: 0,
        };
        let mut body = Vec::new();
        let mut w = Writing { g: &mut *g, m: &m };
        if !w.g.one_in(4) {
            let pat = w.binding(&mut ctx, &param, 2, true, &mut Vec::new());
            ctx.bind(&pat, &param);
            body.push(Stmt::Destructure(pat, None, Expr::Name("p".to_string())));
        }
        for _ in 0..w.g.below(3) {
            body.push(w.statement(&mut ctx, 2));
        }
        body.push(Stmt::Expr(w.value(&mut ctx, &result, 2)));
        m.fns[f].body = body;
    }
    let mut ctx = Ctx {
        names: vec![("note".to_string(), Ty::Str)],
        vars: vec!["note".to_string()],
        callable: 0..count,
        fresh: 0,
    };
    let mut main = Vec::new();
    let mut w = Writing { g, m: &m };
    main.push(Stmt::Var("note".to_string(), w.literal()));
    for f in 0..count {
        let param = m.fns[f].param.clone();
        let arg = w.value(&mut ctx, &param, 2);
        let name = ctx.fresh("r");
        ctx.names.push((name.clone(), m.fns[f].result.clone()));
        main.push(Stmt::Define(name, None, Expr::Call(f, Box::new(arg))));
        for _ in 0..w.g.below(3) {
            main.push(w.statement(&mut ctx, 2));
        }
    }
    if let Some(kind) = kind {
        main.push(mistake(&mut w, &mut ctx, kind));
    }
    main.push(Stmt::Print(Expr::Name("note".to_string())));
    m.main = main;
    m
}

/// Lines for `main` that hold one mistake of the kind.
fn mistake(w: &mut Writing, ctx: &mut Ctx, kind: Kind) -> Stmt {
    let raw = |before: &str, rest: &str, mentions: &str, fix: Option<&str>| {
        Stmt::Raw(
            before.to_string(),
            rest.to_string(),
            mentions.to_string(),
            fix.map(str::to_string),
        )
    };
    match kind {
        Kind::Shape => {
            let (before, rest, n) = *w.g.pick(&[
                ("", "(w, x) = (\"a\", \"b\", \"c\")", 2),
                ("", "(w, x, y) = (\"a\", \"b\")", 3),
                ("", "(w, x) = \"a\"", 2),
                ("(w, ", "(x, y)) = (\"a\", Cat(name: \"b\"))", 2),
            ]);
            raw(before, rest, &format!("tuple of {n}"), None)
        }
        Kind::NotAVar => raw(
            "fixed = \"a\"\n  (",
            "fixed, w) = (\"b\", \"c\")",
            "is not a `var`",
            Some("declare it with `var`"),
        ),
        Kind::VarHolds => raw(
            "(",
            "note, w) = (Cat(name: \"a\"), \"b\")",
            "holds text",
            None,
        ),
        Kind::Twice => {
            if w.g.one_in(2) {
                raw("(w, ", "w) = (\"a\", \"b\")", "already defined", None)
            } else {
                raw(
                    "(var ",
                    "note, w) = (\"a\", \"b\")",
                    "already defined",
                    None,
                )
            }
        }
        Kind::TupleWhereText => {
            let (rest, gives) = *w.g.pick(&[
                ("(\"a\", \"b\"))", "a `(str, str)`"),
                ("(Pet.none, ()))", "a `(Pet, ())`"),
                ("((\"a\", \"b\"), \"c\"))", "a `((str, str), str)`"),
            ]);
            raw("print(", rest, &format!("gives {gives}"), None)
        }
        Kind::LeftOut => {
            let n = 2 + w.g.below(2);
            let at = w.g.below(n);
            let held = (0..n)
                .map(|i| if i == at { "Pet.none" } else { "\"x\"" })
                .collect::<Vec<_>>()
                .join(", ");
            let all = cases();
            let mut kept: Vec<usize> = (0..all.len()).collect();
            let mut left = Vec::new();
            for _ in 0..1 + w.g.below(2) {
                left.push(kept.remove(w.g.below(kept.len())));
            }
            left.sort();
            // The pattern for case `k`, with names from `name` for what
            // the case holds and for the other elements.
            let pattern = |k: usize, name: &mut dyn FnMut() -> String| {
                let items: Vec<String> = (0..n)
                    .map(|i| match (i == at, &all[k].1) {
                        (true, Some(_)) => format!(".{}({})", all[k].0, name()),
                        (true, None) => format!(".{}", all[k].0),
                        (false, _) => name(),
                    })
                    .collect();
                format!("({})", items.join(", "))
            };
            let mut arms = String::new();
            for &k in &kept {
                let row = pattern(k, &mut || ctx.fresh("a"));
                arms.push_str(&format!("    {row} {{ print(\"a\") }}\n"));
            }
            let left: Vec<String> = (left.iter())
                .map(|k| format!("`{}`", pattern(*k, &mut || "_".to_string())))
                .collect();
            let left = sentence(&left);
            Stmt::Raw(
                format!("held = ({held})\n  "),
                format!("match held {{\n{arms}  }}"),
                format!("does not cover {left}"),
                Some(format!("for {left}")),
            )
        }
        Kind::NeverReached => {
            let ty = Ty::Tup(vec![Ty::Pet, Ty::Str]);
            let mut inner = ctx.clone();
            let pats = w.rows(&mut inner, &ty, 2);
            let mut arms = String::new();
            for p in &pats {
                arms.push_str(&format!("    {} {{ print(\"a\") }}\n", pattern_text(p)));
            }
            Stmt::Raw(
                format!("held = (Pet.none, \"x\")\n  match held {{\n{arms}    "),
                format!("{} {{ print(\"b\") }}\n  }}", pattern_text(&pats[0])),
                "never reached".to_string(),
                None,
            )
        }
        Kind::NotBuilt => {
            let (before, rest) = *w.g.pick(&[
                ("w: ", "(str) = \"a\""),
                ("w: ", "(str) -> str = \"a\""),
                ("(", ".none, w) = (Pet.none, \"a\")"),
                ("var twice = \"a\"\n  (twice, ", "twice) = (\"b\", \"c\")"),
            ]);
            raw(before, rest, NOT_BUILT, None)
        }
    }
}

/// The items joined as a sentence joins them.
fn sentence(items: &[String]) -> String {
    match items {
        [] => String::new(),
        [one] => one.clone(),
        [a, b] => format!("{a} and {b}"),
        [rest @ .., last] => format!("{}, and {last}", rest.join(", ")),
    }
}

fn pattern_text(p: &Pat) -> String {
    match p {
        Pat::Bind(name, true) => format!("var {name}"),
        Pat::Bind(name, false) | Pat::Assign(name) => name.clone(),
        Pat::Case(k, inner) => {
            let name = cases()[*k].0;
            match inner {
                Some(inner) => format!(".{name}({})", pattern_text(inner)),
                None => format!(".{name}"),
            }
        }
        Pat::Tuple(items) => {
            let items: Vec<String> = items.iter().map(pattern_text).collect();
            format!("({})", items.join(", "))
        }
    }
}

/// A value as the program holds it.
#[derive(Clone, Debug, PartialEq)]
enum Val {
    Text(Vec<u8>),
    Cat(Vec<u8>),
    Unit,
    Case(usize, Option<Box<Val>>),
    Tup(Vec<Val>),
}

fn text_of(v: Val) -> Vec<u8> {
    match v {
        Val::Text(t) => t,
        v => panic!("{v:?} is not text"),
    }
}

/// Whether the pattern of a `match` matches the value, with what it binds.
fn matches(pat: &Pat, v: &Val, out: &mut Vec<(String, Val)>) -> bool {
    match (pat, v) {
        (Pat::Bind(name, _), v) => {
            out.push((name.clone(), v.clone()));
            true
        }
        (Pat::Case(k, inner), Val::Case(j, holds)) => {
            k == j
                && match (inner, holds) {
                    (Some(p), Some(v)) => matches(p, v, out),
                    _ => true,
                }
        }
        (Pat::Tuple(items), Val::Tup(vs)) => items.iter().zip(vs).all(|(p, v)| matches(p, v, out)),
        _ => false,
    }
}

/// Defines and assigns the names of a pattern of a binding.
fn bind(pat: &Pat, v: Val, env: &mut Vec<(String, Val)>) {
    match (pat, v) {
        (Pat::Bind(name, _), v) => env.push((name.clone(), v)),
        (Pat::Assign(name), v) => {
            let at = env.iter().rposition(|(n, _)| n == name).expect("defined");
            env[at].1 = v;
        }
        (Pat::Tuple(items), Val::Tup(vs)) => {
            for (p, v) in items.iter().zip(vs) {
                bind(p, v, env);
            }
        }
        (p, v) => panic!("{p:?} does not take apart {v:?}"),
    }
}

/// Runs statements, and gives the value of the last if it is an
/// expression.
fn stmts(m: &Model, env: &mut Vec<(String, Val)>, list: &[Stmt], out: &mut Vec<u8>) -> Option<Val> {
    let mut last = None;
    for s in list {
        last = None;
        match s {
            Stmt::Define(name, _, e) | Stmt::Var(name, e) => {
                let v = eval(m, env, e, out);
                env.push((name.clone(), v));
            }
            Stmt::Assign(name, e) => {
                let v = eval(m, env, e, out);
                bind(&Pat::Assign(name.clone()), v, env);
            }
            Stmt::Destructure(pat, _, e) => {
                let v = eval(m, env, e, out);
                bind(pat, v, env);
            }
            Stmt::Print(e) => {
                let v = text_of(eval(m, env, e, out));
                out.extend_from_slice(&v);
                out.push(b'\n');
            }
            Stmt::Expr(e) => last = Some(eval(m, env, e, out)),
            Stmt::Raw(..) => unreachable!("a mistake"),
        }
    }
    last
}

fn eval(m: &Model, env: &mut Vec<(String, Val)>, e: &Expr, out: &mut Vec<u8>) -> Val {
    match e {
        Expr::Lit(pieces) => Val::Text(pieces.iter().flat_map(|p| PIECES[*p].1.bytes()).collect()),
        Expr::Name(name) => (env.iter().rev())
            .find(|(n, _)| n == name)
            .expect("defined")
            .1
            .clone(),
        Expr::Field(of) => match eval(m, env, of, out) {
            Val::Cat(name) => Val::Text(name),
            v => panic!("{v:?} is not a `Cat`"),
        },
        Expr::Cat(name) => Val::Cat(text_of(eval(m, env, name, out))),
        Expr::Unit => Val::Unit,
        Expr::Say(text) => {
            let text = text_of(eval(m, env, text, out));
            out.extend_from_slice(&text);
            out.push(b'\n');
            Val::Unit
        }
        Expr::Case(k, payload) => {
            Val::Case(*k, payload.as_ref().map(|p| Box::new(eval(m, env, p, out))))
        }
        // The elements are worked out from left to right.
        Expr::Tuple(items) => Val::Tup(items.iter().map(|i| eval(m, env, i, out)).collect()),
        Expr::Call(f, arg) => {
            let arg = eval(m, env, arg, out);
            let mut inner = vec![("p".to_string(), arg)];
            stmts(m, &mut inner, &m.fns[*f].body, out).expect("a value")
        }
        Expr::Swap(pair) => match eval(m, env, pair, out) {
            Val::Tup(mut items) => {
                items.reverse();
                Val::Tup(items)
            }
            v => panic!("{v:?} is not a pair"),
        },
        Expr::Match(of, arms) => {
            let v = eval(m, env, of, out);
            for arm in arms {
                let mut bound = Vec::new();
                if matches(&arm.pat, &v, &mut bound) {
                    let outer = env.len();
                    env.extend(bound);
                    let given = stmts(m, env, &arm.stmts, out);
                    env.truncate(outer);
                    // A `match` used as a statement gives nothing that is
                    // read.
                    return given.unwrap_or(Val::Unit);
                }
            }
            unreachable!("the patterns cover every value")
        }
    }
}

/// The source being written, and the mistake once it is placed.
struct Writer {
    text: String,
    mistake: Option<Expected>,
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

    fn expr(&mut self, e: &Expr, indent: usize) {
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
            Expr::Unit => self.push("()"),
            Expr::Say(text) => {
                self.push("say(");
                self.expr(text, indent);
                self.push(")");
            }
            Expr::Case(k, payload) => {
                self.push(&format!("Pet.{}", cases()[*k].0));
                if let Some(p) = payload {
                    self.push("(");
                    self.expr(p, indent);
                    self.push(")");
                }
            }
            Expr::Tuple(items) => {
                self.push("(");
                for (i, item) in items.iter().enumerate() {
                    if i > 0 {
                        self.push(", ");
                    }
                    self.expr(item, indent);
                }
                self.push(")");
            }
            Expr::Call(f, arg) => {
                self.push(&format!("f{f}("));
                self.expr(arg, indent);
                self.push(")");
            }
            Expr::Swap(pair) => {
                self.push("swap(");
                self.expr(pair, indent);
                self.push(")");
            }
            Expr::Match(of, arms) => {
                let pad = "  ".repeat(indent);
                self.push("match ");
                self.expr(of, indent);
                self.push(" {\n");
                for arm in arms {
                    self.push(&pad);
                    self.push("  ");
                    self.push(&pattern_text(&arm.pat));
                    self.push(" {\n");
                    self.stmts(&arm.stmts, indent + 2);
                    self.push(&pad);
                    self.push("  }\n");
                }
                self.push(&pad);
                self.push("}");
            }
        }
    }

    fn stmts(&mut self, list: &[Stmt], indent: usize) {
        let pad = "  ".repeat(indent);
        for s in list {
            self.push(&pad);
            match s {
                Stmt::Define(name, ty, e) => {
                    self.push(name);
                    if let Some(t) = ty {
                        self.push(&format!(": {}", type_text(t)));
                    }
                    self.push(" = ");
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
                Stmt::Destructure(pat, ty, e) => {
                    self.push(&pattern_text(pat));
                    if let Some(t) = ty {
                        self.push(&format!(": {}", type_text(t)));
                    }
                    self.push(" = ");
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
                    self.mark(mentions, fix.clone());
                    self.push(rest);
                }
            }
            self.push("\n");
        }
    }

    fn program(&mut self, m: &Model) {
        self.push("struct Cat {\n  name: str\n}\n\nenum Pet {\n");
        for (name, holds) in cases() {
            self.push(&format!("  {name}"));
            if let Some(t) = holds {
                self.push(&format!(": {}", type_text(&t)));
            }
            self.push("\n");
        }
        self.push("}\n\nfn say(text: str) ! console { print(text) }\n");
        self.push("\nfn swap[A, B](pair: (A, B)) -> (B, A) {\n  (a, b) = pair\n  (b, a)\n}\n");
        self.push("\nfn main() ! console {\n");
        self.stmts(&m.main, 1);
        self.push("}\n");
        for (i, f) in m.fns.iter().enumerate() {
            self.push(&format!("\nfn f{i}(p: {})", type_text(&f.param)));
            if f.written {
                self.push(&format!(" -> {}", type_text(&f.result)));
            }
            self.push(" ! console {\n");
            self.stmts(&f.body, 1);
            self.push("}\n");
        }
    }
}

fn type_text(ty: &Ty) -> String {
    match ty {
        Ty::Str => "str".to_string(),
        Ty::Cat => "Cat".to_string(),
        Ty::Pet => "Pet".to_string(),
        Ty::Unit => "()".to_string(),
        Ty::Tup(items) => {
            let items: Vec<String> = items.iter().map(type_text).collect();
            format!("({})", items.join(", "))
        }
    }
}

fn build(g: &mut Gen, kind: Option<Kind>) -> TupleCase {
    let m = model(g, kind);
    let mut stdout = Vec::new();
    if kind.is_none() {
        stmts(&m, &mut Vec::new(), &m.main, &mut stdout);
    }
    let mut w = Writer {
        text: String::new(),
        mistake: None,
    };
    w.program(&m);
    assert_eq!(
        w.mistake.is_some(),
        kind.is_some(),
        "{kind:?} in {}",
        w.text
    );
    TupleCase {
        source: w.text,
        stdout,
        mistake: w.mistake,
    }
}
