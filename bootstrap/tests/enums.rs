//! The generator of programs with enums and `match`, which can also place
//! one mistake in what it builds.
//!
//! A program has a struct `Cat`, a few enums, some with type parameters,
//! whose cases hold text, a `Cat`, a type parameter, or an earlier enum, and
//! a few functions that each take a value of an enum and take it apart with
//! `match`. The patterns of a `match` cover every value once: some cases,
//! each with the patterns for what it holds, and a bare name for the rest
//! when some case is left out. `main` builds cases, with a leading dot where
//! the type is known and in full elsewhere, passes them to the functions,
//! binds them, and takes them apart with `match` as a statement and as an
//! expression, assigning to a `var` in the blocks of a `match`.
//!
//! The generator first builds a model of the program and works out from it
//! what the program prints. Then it writes the model out twice: as built,
//! placing the mistake as it writes, and with every case written in full.

use crate::generate::Gen;
use crate::programs::{Expected, PIECES, pos_after};

/// A program with enums: its source, the same program with every case
/// written in full, what it prints, and the mistake placed in it, if any.
pub struct EnumCase {
    pub source: String,
    pub in_full: String,
    pub stdout: Vec<u8>,
    pub mistake: Option<Expected>,
}

impl std::fmt::Debug for EnumCase {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self.source)
    }
}

/// A type: text, the struct `Cat`, an enum with a type for each of its type
/// parameters, or a type parameter of the enum the type is written in.
#[derive(Clone, Debug, PartialEq, Eq)]
enum Ty {
    Str,
    Cat,
    E(usize, Vec<Ty>),
    P(usize),
}

impl Ty {
    fn subst(&self, args: &[Ty]) -> Ty {
        match self {
            Ty::P(i) => args[*i].clone(),
            Ty::E(e, of) => Ty::E(*e, of.iter().map(|t| t.subst(args)).collect()),
            t => t.clone(),
        }
    }

    fn mentions(&self, p: usize) -> bool {
        match self {
            Ty::P(i) => *i == p,
            Ty::E(_, args) => args.iter().any(|t| t.mentions(p)),
            _ => false,
        }
    }
}

struct EnumDef {
    name: &'static str,
    params: usize,
    cases: Vec<(&'static str, Option<Ty>)>,
}

/// A pattern: a bare name, or a case by its place and name, with the
/// pattern for what it holds. `Mark` places the mistake where the pattern
/// starts.
#[derive(Clone, Debug)]
enum Pat {
    Bind(String),
    Case(usize, &'static str, Option<Box<Pat>>),
    Mark(Box<Pat>, String),
}

#[derive(Clone, Debug)]
struct ArmM {
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
    /// The construction of a `Cat`.
    Cat(Box<Expr>),
    /// Case `case` of enum `e`, with a leading dot if `dot`, and what it
    /// holds.
    Case {
        e: usize,
        case: usize,
        payload: Option<Box<Expr>>,
        dot: bool,
    },
    /// A call of function `f`.
    Call(usize, Box<Expr>),
    Match(Box<Expr>, Vec<ArmM>),
    /// Text written as it is, with the mistake placed after the first part.
    Raw(String, String, String, Option<String>),
    /// The mistake is placed where the expression starts.
    Mark(Box<Expr>, String, Option<String>),
}

#[derive(Clone, Debug)]
enum Stmt {
    Define(String, Option<Ty>, Expr),
    Var(String, Expr),
    Assign(String, Expr),
    Print(Expr),
    Expr(Expr),
    /// A line written as it is, with the mistake placed after its first
    /// part.
    Raw(String, String, String),
}

struct FnDef {
    param: Ty,
    body: Vec<Stmt>,
}

struct Model {
    enums: Vec<EnumDef>,
    fns: Vec<FnDef>,
    main: Vec<Stmt>,
    /// A definition of its own after the program's, written as the text
    /// before the mistake and the rest, if the mistake is there.
    item: Option<(String, String, String)>,
}

/// The kinds of mistake the generator can place, each once in a file.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Kind {
    CaseLeftOut,
    NeverReached,
    ArmGivesOther,
    EnumNotKnown,
    NoSuchCase,
    CaseHoldsNone,
    NameTaken,
    NotBuilt,
}

const KINDS: &[Kind] = &[
    Kind::CaseLeftOut,
    Kind::NeverReached,
    Kind::ArmGivesOther,
    Kind::EnumNotKnown,
    Kind::NoSuchCase,
    Kind::CaseHoldsNone,
    Kind::NameTaken,
    Kind::NotBuilt,
];

const ENUMS: &[&str] = &["Pet", "Reply", "Shape"];
const CASES: &[&str] = &["cat", "stray", "none", "boxed", "empty", "pair"];
const PARAMS: &[&str] = &["T", "U"];
const NOT_BUILT: &str = "not supported yet";

/// A valid program.
pub fn program(g: &mut Gen) -> EnumCase {
    build(g, None)
}

/// A program with one mistake in it.
pub fn program_with_mistake(g: &mut Gen) -> EnumCase {
    let kind = *g.pick(KINDS);
    build(g, Some(kind))
}

/// The enums: each case holds nothing, text, a `Cat`, one of the enum's type
/// parameters, or an enum before it, and each type parameter is held by a
/// case.
fn enums(g: &mut Gen) -> Vec<EnumDef> {
    let mut out: Vec<EnumDef> = Vec::new();
    for (k, name) in ENUMS.iter().enumerate().take(1 + g.below(3)) {
        let params = if g.one_in(3) { 1 + g.below(2) } else { 0 };
        let count = (1 + g.below(4)).max(params);
        let mut names: Vec<&'static str> = CASES.to_vec();
        let mut cases = Vec::new();
        for c in 0..count {
            let case = names.remove(g.below(names.len()));
            let holds = if c < params {
                Some(Ty::P(c))
            } else {
                match g.below(6) {
                    0 | 1 => None,
                    2 => Some(Ty::Str),
                    3 => Some(Ty::Cat),
                    4 if params > 0 => Some(Ty::P(g.below(params))),
                    _ if k > 0 => {
                        let e = g.below(k);
                        let args = (0..out[e].params)
                            .map(|_| match g.below(3) {
                                0 if params > 0 => Ty::P(g.below(params)),
                                1 => Ty::Cat,
                                _ => Ty::Str,
                            })
                            .collect();
                        Some(Ty::E(e, args))
                    }
                    _ => Some(Ty::Str),
                }
            };
            cases.push((case, holds));
        }
        assert!((0..params).all(|p| {
            cases
                .iter()
                .any(|(_, t)| t.as_ref().is_some_and(|t| t.mentions(p)))
        }));
        out.push(EnumDef {
            name,
            params,
            cases,
        });
    }
    out
}

/// An enum with a type for each of its type parameters: text, a `Cat`, or
/// an enum, while `depth` allows.
fn enum_type(g: &mut Gen, enums: &[EnumDef], depth: usize) -> Ty {
    let e = g.below(enums.len());
    let args = (0..enums[e].params)
        .map(|_| some_type(g, enums, depth.saturating_sub(1)))
        .collect();
    Ty::E(e, args)
}

fn some_type(g: &mut Gen, enums: &[EnumDef], depth: usize) -> Ty {
    match g.below(3) {
        0 => Ty::Str,
        1 => Ty::Cat,
        _ if depth > 0 => enum_type(g, enums, depth),
        _ => Ty::Str,
    }
}

/// What generating an expression needs to know: the names defined, the
/// functions it may call, which come after the one it is in, and a count
/// for fresh names.
#[derive(Clone)]
struct Ctx {
    names: Vec<(String, Ty)>,
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
}

fn literal(g: &mut Gen) -> Expr {
    Expr::Lit((0..g.below(3)).map(|_| g.below(PIECES.len())).collect())
}

/// An expression that gives text, of the function whose calls `ctx`
/// allows.
fn text(g: &mut Gen, m: &Model, ctx: &mut Ctx, depth: usize) -> Expr {
    let texts = ctx.named(&Ty::Str);
    let cats = ctx.named(&Ty::Cat);
    let enums: Vec<(String, Ty)> = (ctx.names.iter())
        .filter(|(_, t)| matches!(t, Ty::E(..)))
        .cloned()
        .collect();
    match g.below(6) {
        1 if !texts.is_empty() => Expr::Name(g.pick(&texts).clone()),
        2 if !cats.is_empty() => Expr::Field(Box::new(Expr::Name(g.pick(&cats).clone()))),
        3 if depth > 0 && !ctx.callable.is_empty() => {
            let f = ctx.callable.start + g.below(ctx.callable.len());
            let arg = value(g, m, ctx, &m.fns[f].param.clone(), depth - 1);
            Expr::Call(f, Box::new(arg))
        }
        4 if depth > 0 && !enums.is_empty() => {
            let (name, ty) = g.pick(&enums).clone();
            let arms = arms(g, m, ctx, &ty, depth - 1, true);
            Expr::Match(Box::new(Expr::Name(name)), arms)
        }
        _ => literal(g),
    }
}

/// An expression of the type, where the type is known, so a case may be
/// written with a leading dot.
fn value(g: &mut Gen, m: &Model, ctx: &mut Ctx, ty: &Ty, depth: usize) -> Expr {
    let named = ctx.named(ty);
    if !named.is_empty() && g.one_in(3) {
        return Expr::Name(g.pick(&named).clone());
    }
    match ty {
        Ty::Str => text(g, m, ctx, depth.saturating_sub(1)),
        Ty::Cat => Expr::Cat(Box::new(text(g, m, ctx, depth.saturating_sub(1)))),
        Ty::E(e, args) => {
            let case = g.below(m.enums[*e].cases.len());
            let holds = m.enums[*e].cases[case].1.as_ref().map(|t| t.subst(args));
            let payload = holds.map(|t| Box::new(value(g, m, ctx, &t, depth.saturating_sub(1))));
            Expr::Case {
                e: *e,
                case,
                payload,
                dot: g.one_in(2),
            }
        }
        Ty::P(_) => unreachable!("a type with no type parameters"),
    }
}

/// Patterns that cover every value of the type once, with none that is
/// never reached: some of its cases, in any order, each with patterns that
/// cover what it holds, and then a bare name if a case is left out. A type
/// that is not an enum, or one past `depth`, gets a bare name alone.
fn rows(g: &mut Gen, m: &Model, ctx: &mut Ctx, ty: &Ty, depth: usize) -> Vec<Pat> {
    let Ty::E(e, args) = ty else {
        return vec![Pat::Bind(ctx.fresh("a"))];
    };
    if depth == 0 {
        return vec![Pat::Bind(ctx.fresh("a"))];
    }
    let cases = &m.enums[*e].cases;
    let mut order: Vec<usize> = (0..cases.len()).collect();
    let mut chosen = Vec::new();
    for _ in 0..cases.len() {
        if g.one_in(4) {
            break;
        }
        chosen.push(order.remove(g.below(order.len())));
    }
    let mut out = Vec::new();
    for &c in &chosen {
        let (name, holds) = &cases[c];
        match holds {
            None => out.push(Pat::Case(c, name, None)),
            Some(t) => {
                let inner = if g.one_in(3) {
                    rows(g, m, ctx, &t.subst(args), depth - 1)
                } else {
                    vec![Pat::Bind(ctx.fresh("a"))]
                };
                for p in inner {
                    out.push(Pat::Case(c, name, Some(Box::new(p))));
                }
            }
        }
    }
    if chosen.len() < cases.len() {
        out.push(Pat::Bind(ctx.fresh("a")));
    }
    out
}

/// The names a pattern binds, with their types, when it matches a value of
/// the type.
fn binds(m: &Model, pat: &Pat, ty: &Ty, out: &mut Vec<(String, Ty)>) {
    match pat {
        Pat::Bind(name) => out.push((name.clone(), ty.clone())),
        Pat::Case(c, _, Some(inner)) => {
            let Ty::E(e, args) = ty else {
                unreachable!("a case of an enum")
            };
            let holds = m.enums[*e].cases[*c].1.as_ref().expect("holds a value");
            binds(m, inner, &holds.subst(args), out);
        }
        Pat::Case(..) => {}
        Pat::Mark(inner, _) => binds(m, inner, ty, out),
    }
}

/// The blocks of a `match` on a value of the type. Each gives text if
/// `gives`, and otherwise prints or assigns to `note`.
fn arms(g: &mut Gen, m: &Model, ctx: &mut Ctx, ty: &Ty, depth: usize, gives: bool) -> Vec<ArmM> {
    let pats = rows(g, m, ctx, ty, 2);
    pats.into_iter()
        .map(|pat| block(g, m, ctx, pat, ty, depth, gives))
        .collect()
}

fn block(
    g: &mut Gen,
    m: &Model,
    ctx: &mut Ctx,
    pat: Pat,
    ty: &Ty,
    depth: usize,
    gives: bool,
) -> ArmM {
    let mut inner = ctx.clone();
    binds(m, &pat, ty, &mut inner.names);
    let mut stmts = Vec::new();
    if g.one_in(3) {
        let name = inner.fresh("t");
        let e = text(g, m, &mut inner, depth);
        stmts.push(Stmt::Define(name.clone(), None, e));
        inner.names.push((name, Ty::Str));
    }
    let last = text(g, m, &mut inner, depth);
    stmts.push(if gives {
        Stmt::Expr(last)
    } else if g.one_in(2) && inner.names.iter().any(|(n, _)| n == "note") {
        Stmt::Assign("note".to_string(), last)
    } else {
        Stmt::Print(last)
    });
    ctx.fresh = inner.fresh;
    ArmM { pat, stmts }
}

fn model(g: &mut Gen, kind: Option<Kind>) -> Model {
    let enums = enums(g);
    let count = 1 + g.below(3);
    let mut m = Model {
        fns: (0..count)
            .map(|_| FnDef {
                param: enum_type(g, &enums, 2),
                body: Vec::new(),
            })
            .collect(),
        enums,
        main: Vec::new(),
        item: None,
    };
    for f in (0..count).rev() {
        let param = m.fns[f].param.clone();
        let mut ctx = Ctx {
            names: vec![("p".to_string(), param.clone())],
            callable: f + 1..count,
            fresh: 0,
        };
        let arms = match (f, kind) {
            (0, Some(Kind::CaseLeftOut)) => {
                // One block or more is left out, and the error names each
                // case they were for, in the order the enum declares them.
                let mut arms = every_case(g, &m, &mut ctx, &param);
                let mut left = Vec::new();
                for _ in 0..1 + g.below(arms.len()) {
                    left.push(arms.remove(g.below(arms.len())).pat);
                }
                let mut left: Vec<(usize, String)> = (left.into_iter())
                    .map(|p| match p {
                        Pat::Case(c, name, Some(_)) => (c, format!("`.{name}(_)`")),
                        Pat::Case(c, name, None) => (c, format!("`.{name}`")),
                        _ => unreachable!("a case"),
                    })
                    .collect();
                left.sort();
                let left: Vec<String> = left.into_iter().map(|(_, l)| l).collect();
                let left = sentence(&left);
                let mentions = format!("does not cover {left}");
                return_mark(&mut m, f, ctx, arms, mentions, Some(format!("for {left}")));
                continue;
            }
            (0, Some(Kind::NameTaken)) => {
                let mut arms = every_case(g, &m, &mut ctx, &param);
                arms.truncate(g.below(arms.len()));
                let last = block(g, &m, &mut ctx, Pat::Bind("p".to_string()), &param, 1, true);
                arms.push(ArmM {
                    pat: Pat::Mark(Box::new(last.pat), "already defined".to_string()),
                    stmts: last.stmts,
                });
                arms
            }
            (0, Some(Kind::NeverReached)) => {
                let mut arms = arms(g, &m, &mut ctx, &param, 1, true);
                let again = block(g, &m, &mut ctx, arms[0].pat.clone(), &param, 1, true);
                arms.push(ArmM {
                    pat: Pat::Mark(Box::new(again.pat), "never reached".to_string()),
                    stmts: again.stmts,
                });
                arms
            }
            (0, Some(Kind::ArmGivesOther)) => {
                let mut arms = arms(g, &m, &mut ctx, &param, 1, true);
                let at = g.below(arms.len());
                let last = arms[at].stmts.last_mut().expect("a statement");
                *last = Stmt::Expr(Expr::Mark(
                    Box::new(Expr::Raw(
                        String::new(),
                        "Cat(name: \"x\")".to_string(),
                        String::new(),
                        None,
                    )),
                    "gives a `Cat`".to_string(),
                    None,
                ));
                arms
            }
            _ => arms(g, &m, &mut ctx, &param, 1, true),
        };
        let matched = Expr::Match(Box::new(Expr::Name("p".to_string())), arms);
        m.fns[f].body = if kind != Some(Kind::ArmGivesOther) && g.one_in(3) {
            vec![
                Stmt::Define("r".to_string(), None, matched),
                Stmt::Expr(Expr::Name("r".to_string())),
            ]
        } else {
            vec![Stmt::Expr(matched)]
        };
    }
    m.main = main_body(g, &m, kind);
    if kind == Some(Kind::NotBuilt) {
        match g.below(4) {
            0 => m
                .main
                .push(Stmt::Raw("w = ".into(), "[note]".into(), NOT_BUILT.into())),
            1 => m.main.push(Stmt::Raw(
                "w = ".into(),
                "for i { note }".into(),
                NOT_BUILT.into(),
            )),
            2 => m.main.push(Stmt::Raw(
                "w: ".into(),
                "List[str] = .none".into(),
                NOT_BUILT.into(),
            )),
            _ => {
                m.main.push(Stmt::Raw(
                    String::new(),
                    "w: Loop = Loop.end".into(),
                    String::new(),
                ));
                m.item = Some((
                    "enum Loop {\n  next: ".into(),
                    "Loop\n  end\n}".into(),
                    NOT_BUILT.into(),
                ));
            }
        }
    }
    m
}

/// A block for every case of the enum, in any order, each naming what it
/// holds with a bare name.
fn every_case(g: &mut Gen, m: &Model, ctx: &mut Ctx, ty: &Ty) -> Vec<ArmM> {
    let Ty::E(e, _) = ty else {
        unreachable!("an enum")
    };
    let mut order: Vec<usize> = (0..m.enums[*e].cases.len()).collect();
    let mut out = Vec::new();
    while !order.is_empty() {
        let c = order.remove(g.below(order.len()));
        let (name, holds) = &m.enums[*e].cases[c];
        let inner = holds.as_ref().map(|_| Box::new(Pat::Bind(ctx.fresh("a"))));
        out.push(block(g, m, ctx, Pat::Case(c, name, inner), ty, 1, true));
    }
    out
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

/// Makes the body of function `f` a `match` with these blocks, whose
/// mistake is at `match`.
fn return_mark(
    m: &mut Model,
    f: usize,
    _ctx: Ctx,
    arms: Vec<ArmM>,
    mentions: String,
    fix: Option<String>,
) {
    let matched = Expr::Match(Box::new(Expr::Name("p".to_string())), arms);
    m.fns[f].body = vec![Stmt::Expr(Expr::Mark(Box::new(matched), mentions, fix))];
}

/// Whether a value of the type can be written where nothing says its type:
/// a case of an enum with no type parameters, in full.
fn bindable(ty: &Ty, m: &Model) -> bool {
    match ty {
        Ty::E(e, _) => m.enums[*e].params == 0,
        _ => true,
    }
}

fn main_body(g: &mut Gen, m: &Model, kind: Option<Kind>) -> Vec<Stmt> {
    let mut ctx = Ctx {
        names: Vec::new(),
        callable: 0..m.fns.len(),
        fresh: 0,
    };
    let mut out = vec![Stmt::Var("note".to_string(), literal(g))];
    ctx.names.push(("note".to_string(), Ty::Str));
    let first = match kind {
        Some(Kind::NoSuchCase) => no_such_case(g, m),
        Some(Kind::CaseHoldsNone) => {
            let Ty::E(e, _) = &m.fns[0].param else {
                unreachable!("an enum")
            };
            let holding: Vec<usize> = (0..m.enums[*e].cases.len())
                .filter(|c| m.enums[*e].cases[*c].1.is_some())
                .collect();
            if holding.is_empty() {
                no_such_case(g, m)
            } else {
                let name = m.enums[*e].cases[*g.pick(&holding)].0;
                Expr::Raw(String::new(), format!(".{name}"), "holds".to_string(), None)
            }
        }
        _ => {
            let param = m.fns[0].param.clone();
            value(g, m, &mut ctx, &param, 2)
        }
    };
    out.push(Stmt::Print(Expr::Call(0, Box::new(first))));
    if kind == Some(Kind::EnumNotKnown) {
        let e = g.below(m.enums.len());
        let (name, holds) = &m.enums[e].cases[g.below(m.enums[e].cases.len())];
        let written = match holds {
            Some(_) => format!(".{name}(\"x\")"),
            None => format!(".{name}"),
        };
        let fix = Some("write the enum's name before the dot".to_string());
        out.push(Stmt::Define(
            "w".to_string(),
            None,
            Expr::Raw(String::new(), written, "not known".to_string(), fix),
        ));
    }
    for _ in 0..g.below(5) {
        let enums: Vec<(String, Ty)> = (ctx.names.iter())
            .filter(|(_, t)| matches!(t, Ty::E(..)))
            .cloned()
            .collect();
        match g.below(5) {
            0 => {
                let f = g.below(m.fns.len());
                let param = m.fns[f].param.clone();
                let arg = value(g, m, &mut ctx, &param, 2);
                out.push(Stmt::Print(Expr::Call(f, Box::new(arg))));
            }
            1 if !enums.is_empty() => {
                let (name, ty) = g.pick(&enums).clone();
                let arms = arms(g, m, &mut ctx, &ty, 1, false);
                out.push(Stmt::Expr(Expr::Match(Box::new(Expr::Name(name)), arms)));
            }
            2 if !enums.is_empty() => {
                let (name, ty) = g.pick(&enums).clone();
                let arms = arms(g, m, &mut ctx, &ty, 1, true);
                out.push(Stmt::Print(Expr::Match(Box::new(Expr::Name(name)), arms)));
            }
            3 if !enums.is_empty() => {
                let (name, ty) = g.pick(&enums).clone();
                let arms = arms(g, m, &mut ctx, &ty, 1, true);
                let x = ctx.fresh("x");
                out.push(Stmt::Define(
                    x.clone(),
                    None,
                    Expr::Match(Box::new(Expr::Name(name)), arms),
                ));
                out.push(Stmt::Print(Expr::Name(x.clone())));
                ctx.names.push((x, Ty::Str));
            }
            _ => {
                let ty = if g.one_in(2) {
                    m.fns[g.below(m.fns.len())].param.clone()
                } else {
                    enum_type(g, &m.enums, 2)
                };
                let name = ctx.fresh("v");
                let mut e = value(g, m, &mut ctx, &ty, 2);
                let typed = !bindable(&ty, m) || !matches!(e, Expr::Case { .. }) || g.one_in(2);
                if let Expr::Case { dot, .. } = &mut e
                    && !typed
                {
                    *dot = false;
                }
                out.push(Stmt::Define(name.clone(), typed.then(|| ty.clone()), e));
                ctx.names.push((name, ty));
            }
        }
    }
    out.push(Stmt::Print(Expr::Name("note".to_string())));
    out
}

/// An argument for the first function that names a case its enum lacks.
fn no_such_case(g: &mut Gen, m: &Model) -> Expr {
    let Ty::E(e, _) = &m.fns[0].param else {
        unreachable!("an enum")
    };
    let before = if g.one_in(2) {
        ".".to_string()
    } else {
        format!("{}.", m.enums[*e].name)
    };
    Expr::Raw(
        before,
        "zebra".to_string(),
        "no case named `zebra`".to_string(),
        None,
    )
}

/// A value as the program holds it.
#[derive(Clone, Debug, PartialEq)]
enum Val {
    Text(Vec<u8>),
    Cat(Vec<u8>),
    Case(usize, Option<Box<Val>>),
}

fn text_of(v: Val) -> Vec<u8> {
    match v {
        Val::Text(t) => t,
        v => panic!("{v:?} is not text"),
    }
}

/// Whether the pattern matches the value, with what it binds.
fn matches(pat: &Pat, v: &Val, out: &mut Vec<(String, Val)>) -> bool {
    match (pat, v) {
        (Pat::Bind(name), v) => {
            out.push((name.clone(), v.clone()));
            true
        }
        (Pat::Case(c, _, inner), Val::Case(d, holds)) => {
            c == d
                && match (inner, holds) {
                    (Some(p), Some(v)) => matches(p, v, out),
                    _ => true,
                }
        }
        (Pat::Mark(p, _), v) => matches(p, v, out),
        _ => false,
    }
}

fn run(m: &Model, f: usize, arg: Val, out: &mut Vec<u8>) -> Val {
    let mut env = vec![("p".to_string(), arg)];
    stmts(m, &mut env, &m.fns[f].body, out).expect("a value")
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
                let at = env.iter().rposition(|(n, _)| n == name).expect("defined");
                env[at].1 = v;
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
        Expr::Name(name) => env
            .iter()
            .rev()
            .find(|(n, _)| n == name)
            .expect("defined")
            .1
            .clone(),
        Expr::Field(of) => match eval(m, env, of, out) {
            Val::Cat(name) => Val::Text(name),
            v => panic!("{v:?} is not a `Cat`"),
        },
        Expr::Cat(name) => Val::Cat(text_of(eval(m, env, name, out))),
        Expr::Case { case, payload, .. } => Val::Case(
            *case,
            payload.as_ref().map(|p| Box::new(eval(m, env, p, out))),
        ),
        Expr::Call(f, arg) => {
            let arg = eval(m, env, arg, out);
            run(m, *f, arg, out)
        }
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
                    return given.unwrap_or(Val::Text(Vec::new()));
                }
            }
            unreachable!("the patterns cover every value")
        }
        Expr::Raw(..) | Expr::Mark(..) => unreachable!("a mistake"),
    }
}

/// The source being written, and the mistake once it is placed.
struct Writer<'a> {
    m: &'a Model,
    text: String,
    /// Whether every case is written in full.
    full: bool,
    mistake: Option<Expected>,
}

impl Writer<'_> {
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

    fn ty(&self, ty: &Ty, params: &[&str]) -> String {
        match ty {
            Ty::Str => "str".to_string(),
            Ty::Cat => "Cat".to_string(),
            Ty::P(p) => params[*p].to_string(),
            Ty::E(e, args) => {
                let name = self.m.enums[*e].name;
                if args.is_empty() {
                    name.to_string()
                } else {
                    let args: Vec<String> = args.iter().map(|t| self.ty(t, params)).collect();
                    format!("{name}[{}]", args.join(", "))
                }
            }
        }
    }

    fn pattern(&mut self, p: &Pat) {
        match p {
            Pat::Bind(name) => self.push(name),
            Pat::Case(_, name, inner) => {
                self.push(&format!(".{name}"));
                if let Some(inner) = inner {
                    self.push("(");
                    self.pattern(inner);
                    self.push(")");
                }
            }
            Pat::Mark(inner, mentions) => {
                self.mark(mentions, None);
                self.pattern(inner);
            }
        }
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
            Expr::Case {
                e,
                case,
                payload,
                dot,
            } => {
                let def = &self.m.enums[*e];
                if !*dot || self.full {
                    self.push(def.name);
                }
                self.push(&format!(".{}", def.cases[*case].0));
                if let Some(p) = payload {
                    self.push("(");
                    self.expr(p, indent);
                    self.push(")");
                }
            }
            Expr::Call(f, arg) => {
                self.push(&format!("f{f}("));
                self.expr(arg, indent);
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
                    self.pattern(&arm.pat);
                    self.push(" {\n");
                    self.stmts(&arm.stmts, indent + 2);
                    self.push(&pad);
                    self.push("  }\n");
                }
                self.push(&pad);
                self.push("}");
            }
            Expr::Raw(before, rest, mentions, fix) => {
                self.push(before);
                if !mentions.is_empty() {
                    self.mark(mentions, fix.clone());
                }
                self.push(rest);
            }
            Expr::Mark(inner, mentions, fix) => {
                self.mark(mentions, fix.clone());
                self.expr(inner, indent);
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
                        let t = self.ty(t, &[]);
                        self.push(&format!(": {t}"));
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
                Stmt::Print(e) => {
                    self.push("print(");
                    self.expr(e, indent);
                    self.push(")");
                }
                Stmt::Expr(e) => self.expr(e, indent),
                Stmt::Raw(before, rest, mentions) => {
                    self.push(before);
                    if !mentions.is_empty() {
                        self.mark(mentions, None);
                    }
                    self.push(rest);
                }
            }
            self.push("\n");
        }
    }

    fn program(&mut self) {
        let m = self.m;
        self.push("struct Cat {\n  name: str\n}\n");
        for def in &m.enums {
            self.push(&format!("\nenum {}", def.name));
            if def.params > 0 {
                self.push(&format!("[{}]", PARAMS[..def.params].join(", ")));
            }
            self.push(" {\n");
            for (name, holds) in &def.cases {
                self.push(&format!("  {name}"));
                if let Some(t) = holds {
                    let t = self.ty(t, &PARAMS[..def.params]);
                    self.push(&format!(": {t}"));
                }
                self.push("\n");
            }
            self.push("}\n");
        }
        self.push("\nfn main() ! console {\n");
        self.stmts(&m.main, 1);
        self.push("}\n");
        for (i, f) in m.fns.iter().enumerate() {
            let param = self.ty(&f.param, &[]);
            self.push(&format!("\nfn f{i}(p: {param}) -> str {{\n"));
            self.stmts(&f.body, 1);
            self.push("}\n");
        }
        if let Some((before, rest, mentions)) = &m.item {
            self.push("\n");
            self.push(before);
            self.mark(mentions, None);
            self.push(rest);
            self.push("\n");
        }
    }
}

fn build(g: &mut Gen, kind: Option<Kind>) -> EnumCase {
    let m = model(g, kind);
    let mut stdout = Vec::new();
    if kind.is_none() {
        stmts(&m, &mut Vec::new(), &m.main, &mut stdout);
    }
    let write = |full: bool| {
        let mut w = Writer {
            m: &m,
            text: String::new(),
            full,
            mistake: None,
        };
        w.program();
        (w.text, w.mistake)
    };
    let (source, mistake) = write(false);
    let (in_full, _) = write(true);
    assert_eq!(mistake.is_some(), kind.is_some(), "{kind:?} in {source}");
    EnumCase {
        source,
        in_full,
        stdout,
        mistake,
    }
}
