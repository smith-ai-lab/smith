//! The generator of programs with traits, implementations, and functions
//! and structs that take type parameters, which can also place one mistake
//! in what it builds.
//!
//! It first builds a model of the program. From the model it works out
//! each copy of a function that the program runs, one for each choice of
//! types, and from those it writes the same program out with one function
//! and one struct for each type used, and works out what the program
//! prints. Then it writes the model out as source, placing the mistake as
//! it writes.
//!
//! A trait's function may give its parameter `note` a default, which a
//! call may leave out, and which the program written out gives.
//!
//! A function or a trait calls only functions and traits that come after
//! it in a fixed order, and an implementation calls its own trait only for
//! a type parameter it holds, so no copy of a function calls itself.

use smith::diag::Pos;

use crate::generate::Gen;
use crate::programs::{Expected, PIECES, lines_added, pos_after, shifted, with_imports};

/// A program with traits: its source, the same program written out with
/// one function and one struct for each type used, what it prints, and
/// how many copies of functions it runs, `main` included.
pub struct TraitCase {
    pub source: String,
    pub written_out: String,
    pub stdout: Vec<u8>,
    pub copies: usize,
    pub mistake: Option<Expected>,
}

impl std::fmt::Debug for TraitCase {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self.source)
    }
}

/// A type: text, a struct with a type for each of its type parameters, or
/// a type parameter of the definition the type is in, by its place.
#[derive(Clone, Debug, PartialEq, Eq)]
enum Ty {
    Str,
    S(usize, Vec<Ty>),
    P(usize),
}

impl Ty {
    fn subst(&self, args: &[Ty]) -> Ty {
        match self {
            Ty::P(i) => args[*i].clone(),
            Ty::S(s, of) => Ty::S(*s, of.iter().map(|t| t.subst(args)).collect()),
            Ty::Str => Ty::Str,
        }
    }

    fn mentions(&self, p: usize) -> bool {
        match self {
            Ty::P(i) => *i == p,
            Ty::S(_, args) => args.iter().any(|t| t.mentions(p)),
            Ty::Str => false,
        }
    }
}

/// Matches `pattern`, whose type parameters stand for the types in `sol`,
/// against `given`, filling in `sol`.
fn unify(pattern: &Ty, given: &Ty, sol: &mut [Option<Ty>]) -> bool {
    match (pattern, given) {
        (Ty::P(i), given) => match &sol[*i] {
            None => {
                sol[*i] = Some(given.clone());
                true
            }
            Some(t) => t == given,
        },
        (Ty::S(s, a), Ty::S(t, b)) => s == t && a.iter().zip(b).all(|(a, b)| unify(a, b, sol)),
        (a, b) => a == b,
    }
}

struct StructDef {
    name: &'static str,
    params: Vec<&'static str>,
    fields: Vec<(&'static str, Ty)>,
}

struct TraitFn {
    name: &'static str,
    /// Its parameters, whose types are among the trait's types. A second
    /// one is `note`, of type text.
    params: Vec<(&'static str, Ty)>,
    /// The default of `note`, as the pieces of a literal, if it has one.
    default: Option<Vec<usize>>,
    result: Ty,
    console: bool,
}

struct TraitDef {
    name: &'static str,
    params: Vec<&'static str>,
    /// Whether its last type is `out`.
    out: bool,
    fns: Vec<TraitFn>,
    rank: usize,
}

/// A type parameter and the trait it is bound by, if any.
type Generic = (&'static str, Option<usize>);

struct ImplDef {
    of: usize,
    generics: Vec<Generic>,
    /// The trait's types, among the implementation's type parameters.
    args: Vec<Ty>,
    /// The body of each of the trait's functions.
    bodies: Vec<Vec<Stmt>>,
    /// Whether it is a second implementation for types another covers, the
    /// mistake, which no call chooses.
    second: bool,
}

struct FnDef {
    name: String,
    rank: usize,
    generics: Vec<Generic>,
    params: Vec<(&'static str, Ty)>,
    result: Option<Ty>,
    console: bool,
    body: Vec<Stmt>,
}

struct Model {
    structs: Vec<StructDef>,
    traits: Vec<TraitDef>,
    impls: Vec<ImplDef>,
    fns: Vec<FnDef>,
    /// The struct no trait is implemented for, when the mistake needs one.
    stray: Option<usize>,
}

enum Stmt {
    Define {
        name: String,
        ty: Ty,
        typed: bool,
        value: Expr,
    },
    Expr(Expr),
}

enum Expr {
    Text(Vec<usize>),
    Name(String),
    Print(Box<Expr>),
    /// A call of function `f`, with the types its type parameters stand for.
    Call {
        f: usize,
        types: Vec<Ty>,
        args: Vec<Arg>,
    },
    /// A call of function `func` of trait `of`, with the trait's types.
    TraitCall {
        of: usize,
        func: usize,
        types: Vec<Ty>,
        args: Vec<Arg>,
    },
    Construct {
        s: usize,
        types: Vec<Ty>,
        fields: Vec<(usize, Expr)>,
    },
    Field {
        of: Box<Expr>,
        s: usize,
        field: usize,
    },
}

struct Arg {
    param: usize,
    named: bool,
    value: Expr,
}

/// The kinds of mistake the generator can place, each once in a file.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Kind {
    /// A call of a trait's function for a type with no implementation.
    NoImpl,
    /// A call of a function whose bound asks for an implementation that
    /// does not exist.
    BoundUnmet,
    LacksFn,
    ExtraFn,
    SignatureDiffers,
    BoundNotTrait,
    SecondImpl,
    /// A call of a function two traits share, written without its trait.
    Clash,
    /// A binding that states a type other than the `out` type an
    /// implementation fixes.
    OutFixed,
    /// A part of traits the compiler does not build yet.
    NotBuilt,
    /// A default written in an implementation.
    ImplDefault,
}

const KINDS: &[Kind] = &[
    Kind::NoImpl,
    Kind::BoundUnmet,
    Kind::LacksFn,
    Kind::ExtraFn,
    Kind::SignatureDiffers,
    Kind::BoundNotTrait,
    Kind::SecondImpl,
    Kind::Clash,
    Kind::OutFixed,
    Kind::NotBuilt,
    Kind::ImplDefault,
];

const CONCRETE: &[&str] = &["Cat", "Dog", "Bird"];
const FIELDS: &[&str] = &["name", "sound", "home"];
const TRAITS: &[&str] = &["Named", "Shown", "Sized"];
const TRAIT_FNS: &[&str] = &["name_of", "describe", "describe_all"];
const PARAMS: &[&str] = &["a", "b", "c", "d", "e"];
const GENERICS: &[&str] = &["T", "U"];
const DEPTH: usize = 3;

const NOT_BUILT: &str = "not supported yet";

/// A valid program with traits.
pub fn program(g: &mut Gen) -> TraitCase {
    build(g, None)
}

/// A program with traits, with one mistake in it.
pub fn program_with_mistake(g: &mut Gen) -> TraitCase {
    let kind = *g.pick(KINDS);
    build(g, Some(kind))
}

// Building the model.

impl Model {
    fn concrete(&self) -> Vec<usize> {
        (0..self.structs.len())
            .filter(|&s| self.structs[s].params.is_empty() && Some(s) != self.stray)
            .collect()
    }

    fn generic(&self) -> Vec<usize> {
        (0..self.structs.len())
            .filter(|&s| !self.structs[s].params.is_empty())
            .collect()
    }

    /// Whether more than one trait has a function with this name, so a call
    /// of it names its trait.
    fn clashes(&self, name: &str) -> bool {
        (self.traits.iter())
            .filter(|t| t.fns.iter().any(|f| f.name == name))
            .count()
            > 1
    }

    /// The types of trait `k` for `x`, and the implementation with its
    /// types, unless a bound in `scope` promises it.
    #[allow(clippy::type_complexity)]
    fn holds(
        &self,
        k: usize,
        x: &Ty,
        scope: &[Option<usize>],
    ) -> Option<(Vec<Ty>, Option<(usize, Vec<Ty>)>)> {
        let n = self.traits[k].params.len();
        if let Ty::P(p) = x
            && scope[*p] == Some(k)
        {
            return Some((vec![x.clone(); n], None));
        }
        for (i, im) in self.impls.iter().enumerate() {
            if im.of != k || im.second {
                continue;
            }
            let mut sol = vec![None; im.generics.len()];
            if !unify(&im.args[0], x, &mut sol) {
                continue;
            }
            let sol: Vec<Ty> = sol.into_iter().map(|t| t.expect("chosen")).collect();
            for (g, (_, bound)) in im.generics.iter().enumerate() {
                if let Some(b) = bound
                    && !self.meets(*b, &sol[g], scope)
                {
                    return None;
                }
            }
            let all = im.args.iter().map(|t| t.subst(&sol)).collect();
            return Some((all, Some((i, sol))));
        }
        None
    }

    /// Whether `x` meets the bound of trait `k`, which asks for an
    /// implementation whose every type is `x`.
    fn meets(&self, k: usize, x: &Ty, scope: &[Option<usize>]) -> bool {
        self.holds(k, x, scope)
            .is_some_and(|(all, _)| all.iter().all(|t| t == x))
    }
}

/// A type among `params` type parameters.
fn some_type(g: &mut Gen, m: &Model, params: usize, depth: usize) -> Ty {
    let concrete = m.concrete();
    let generic = m.generic();
    match g.below(4) {
        1 if !concrete.is_empty() => Ty::S(*g.pick(&concrete), Vec::new()),
        2 if params > 0 => Ty::P(g.below(params)),
        3 if depth > 0 && !generic.is_empty() => {
            let s = *g.pick(&generic);
            let args = (0..m.structs[s].params.len())
                .map(|_| some_type(g, m, params, depth - 1))
                .collect();
            Ty::S(s, args)
        }
        _ => Ty::Str,
    }
}

/// Where a body is, and what it may use.
struct Ctx {
    rank: usize,
    /// The bound of each type parameter in scope.
    scope: Vec<Option<usize>>,
    names: Vec<(String, Ty)>,
    console: bool,
    /// The trait whose implementation the body is in, if it is in one.
    own: Option<usize>,
}

/// The types a call may be chosen for.
fn candidates(m: &Model, ctx: &Ctx) -> Vec<Ty> {
    let mut base = vec![Ty::Str];
    base.extend(m.concrete().into_iter().map(|s| Ty::S(s, Vec::new())));
    base.extend((0..ctx.scope.len()).map(Ty::P));
    let mut out = base.clone();
    for s in m.generic() {
        let n = m.structs[s].params.len();
        for b in &base {
            out.push(Ty::S(s, vec![b.clone(); n]));
        }
    }
    out
}

/// One of the items, or `None` if there are none.
fn pick_some<'a, T>(g: &mut Gen, items: &'a [T]) -> Option<&'a T> {
    (!items.is_empty()).then(|| g.pick(items))
}

fn literal(g: &mut Gen) -> Expr {
    let count = g.below(6);
    Expr::Text((0..count).map(|_| g.below(PIECES.len())).collect())
}

/// An expression of type `ty`.
fn value(g: &mut Gen, m: &Model, ctx: &Ctx, ty: &Ty, depth: usize) -> Expr {
    let choice = g.below(5);
    let tried = match choice {
        1 => {
            let named: Vec<&String> = (ctx.names.iter())
                .filter(|(_, t)| t == ty)
                .map(|(n, _)| n)
                .collect();
            (!named.is_empty()).then(|| Expr::Name(g.pick(&named).to_string()))
        }
        2 if depth > 0 => call(g, m, ctx, ty, depth),
        3 if depth > 0 => trait_call(g, m, ctx, ty, depth),
        4 if depth > 0 => field_read(g, m, ctx, ty, depth),
        _ => None,
    };
    tried.unwrap_or_else(|| simplest(g, m, ctx, ty, depth))
}

/// An expression of type `ty` that always exists: a literal, a
/// construction, or a field read from a name.
fn simplest(g: &mut Gen, m: &Model, ctx: &Ctx, ty: &Ty, depth: usize) -> Expr {
    match ty {
        Ty::Str => literal(g),
        Ty::S(s, types) => construct(g, m, ctx, *s, types, depth),
        Ty::P(p) => {
            let (name, t) = (ctx.names.iter())
                .find(|(_, t)| t.mentions(*p))
                .expect("a name holds every type parameter");
            path(m, Expr::Name(name.clone()), t, *p)
        }
    }
}

/// The field reads from `from`, of type `t`, that give a value of type
/// parameter `p`.
fn path(m: &Model, from: Expr, t: &Ty, p: usize) -> Expr {
    match t {
        Ty::P(_) => from,
        Ty::S(s, args) => {
            let def = &m.structs[*s];
            let (field, ty) = (def.fields.iter().enumerate())
                .map(|(i, (_, f))| (i, f.subst(args)))
                .find(|(_, f)| f.mentions(p))
                .expect("a field holds each type parameter");
            let read = Expr::Field {
                of: Box::new(from),
                s: *s,
                field,
            };
            path(m, read, &ty, p)
        }
        Ty::Str => unreachable!("text holds no type parameter"),
    }
}

fn construct(g: &mut Gen, m: &Model, ctx: &Ctx, s: usize, types: &[Ty], depth: usize) -> Expr {
    let mut rest: Vec<usize> = (0..m.structs[s].fields.len()).collect();
    let mut fields = Vec::new();
    while !rest.is_empty() {
        let field = rest.remove(g.below(rest.len()));
        let ty = m.structs[s].fields[field].1.subst(types);
        fields.push((field, value(g, m, ctx, &ty, depth.saturating_sub(1))));
    }
    Expr::Construct {
        s,
        types: types.to_vec(),
        fields,
    }
}

/// Arguments for parameters of these types, some by position and the
/// rest by name in any order.
fn args(g: &mut Gen, m: &Model, ctx: &Ctx, params: &[Ty], depth: usize) -> Vec<Arg> {
    let count = params.len();
    let by_position = count - g.below(count + 1);
    let mut rest: Vec<usize> = (by_position..count).collect();
    let mut order: Vec<usize> = (0..by_position).collect();
    while !rest.is_empty() {
        order.push(rest.remove(g.below(rest.len())));
    }
    (order.into_iter())
        .map(|param| Arg {
            param,
            named: param >= by_position,
            value: value(g, m, ctx, &params[param], depth - 1),
        })
        .collect()
}

/// Arguments for function `func` of trait `of`, for parameters of these
/// types, which may leave out `note` if it has a default.
fn trait_args(
    g: &mut Gen,
    m: &Model,
    ctx: &Ctx,
    (of, func): (usize, usize),
    params: &[Ty],
    depth: usize,
) -> Vec<Arg> {
    let mut args = args(g, m, ctx, params, depth);
    if m.traits[of].fns[func].default.is_some() && g.one_in(2) {
        // `note` is the last parameter, so the rest stay as they were.
        args.retain(|a| a.param != 1);
    }
    args
}

/// A call of a later function that gives `ty`, if one can.
fn call(g: &mut Gen, m: &Model, ctx: &Ctx, ty: &Ty, depth: usize) -> Option<Expr> {
    let fits: Vec<usize> = (1..m.fns.len())
        .filter(|&f| {
            let def = &m.fns[f];
            let mut sol = vec![None; def.generics.len()];
            def.rank > ctx.rank
                && (ctx.console || !def.console)
                && def.result.as_ref().is_some_and(|r| unify(r, ty, &mut sol))
        })
        .collect();
    let f = *pick_some(g, &fits)?;
    let def = &m.fns[f];
    let mut sol = vec![None; def.generics.len()];
    unify(def.result.as_ref().expect("gives"), ty, &mut sol);
    let mut types = Vec::new();
    for (i, (_, bound)) in def.generics.iter().enumerate() {
        let t = match sol[i].clone() {
            Some(t) => t,
            None => {
                let options: Vec<Ty> = (candidates(m, ctx).into_iter())
                    .filter(|t| bound.is_none_or(|b| m.meets(b, t, &ctx.scope)))
                    .collect();
                pick_some(g, &options)?.clone()
            }
        };
        if let Some(b) = bound
            && !m.meets(*b, &t, &ctx.scope)
        {
            return None;
        }
        types.push(t);
    }
    let params: Vec<Ty> = def.params.iter().map(|(_, t)| t.subst(&types)).collect();
    let args = args(g, m, ctx, &params, depth);
    Some(Expr::Call { f, types, args })
}

/// A call of a trait's function that gives `ty`, if one can.
fn trait_call(g: &mut Gen, m: &Model, ctx: &Ctx, ty: &Ty, depth: usize) -> Option<Expr> {
    let mut options = Vec::new();
    for (k, t) in m.traits.iter().enumerate() {
        let own = ctx.own == Some(k);
        if t.rank <= ctx.rank && !own {
            continue;
        }
        for (func, f) in t.fns.iter().enumerate() {
            if f.console && !ctx.console {
                continue;
            }
            for x in candidates(m, ctx) {
                // An implementation calls its own trait only for a type
                // parameter it holds, which is smaller than its own type.
                if own && !matches!(x, Ty::P(p) if ctx.scope[p] == Some(k)) {
                    continue;
                }
                if let Some((all, _)) = m.holds(k, &x, &ctx.scope)
                    && f.result.subst(&all) == *ty
                {
                    options.push((k, func, all));
                }
            }
        }
    }
    let (of, func, types) = pick_some(g, &options)?.clone();
    let params: Vec<Ty> = (m.traits[of].fns[func].params.iter())
        .map(|(_, t)| t.subst(&types))
        .collect();
    let args = trait_args(g, m, ctx, (of, func), &params, depth);
    Some(Expr::TraitCall {
        of,
        func,
        types,
        args,
    })
}

/// A read of a field of type `ty` from a struct, if a struct has one.
fn field_read(g: &mut Gen, m: &Model, ctx: &Ctx, ty: &Ty, depth: usize) -> Option<Expr> {
    let mut options: Vec<(usize, usize)> = Vec::new();
    for (s, def) in m.structs.iter().enumerate() {
        if Some(s) == m.stray {
            continue;
        }
        for (field, (_, t)) in def.fields.iter().enumerate() {
            if matches!(t, Ty::P(_)) || t == ty {
                options.push((s, field));
            }
        }
    }
    let &(s, field) = pick_some(g, &options)?;
    let def = &m.structs[s];
    let mut types: Vec<Option<Ty>> = vec![None; def.params.len()];
    if let Ty::P(k) = def.fields[field].1 {
        types[k] = Some(ty.clone());
    }
    let types: Vec<Ty> = (types.into_iter())
        .map(|t| t.unwrap_or_else(|| some_type(g, m, ctx.scope.len(), 0)))
        .collect();
    let of = value(g, m, ctx, &Ty::S(s, types), depth - 1);
    Some(Expr::Field {
        of: Box::new(of),
        s,
        field,
    })
}

/// The statements of a body that gives `result`, if anything.
fn body(g: &mut Gen, m: &Model, mut ctx: Ctx, result: Option<&Ty>) -> Vec<Stmt> {
    let mut stmts = Vec::new();
    for _ in 0..g.below(3) {
        if ctx.console && g.one_in(2) {
            let text = value(g, m, &ctx, &Ty::Str, DEPTH);
            stmts.push(Stmt::Expr(Expr::Print(Box::new(text))));
        } else {
            let name = format!("x{}", stmts.len() + 1);
            let ty = some_type(g, m, ctx.scope.len(), 1);
            let value = value(g, m, &ctx, &ty, DEPTH);
            ctx.names.push((name.clone(), ty.clone()));
            stmts.push(Stmt::Define {
                name,
                ty,
                typed: g.one_in(2),
                value,
            });
        }
    }
    if let Some(ty) = result {
        stmts.push(Stmt::Expr(value(g, m, &ctx, ty, DEPTH)));
    }
    stmts
}

fn model(g: &mut Gen, kind: Option<Kind>) -> Model {
    let mut m = Model {
        structs: Vec::new(),
        traits: Vec::new(),
        impls: Vec::new(),
        fns: Vec::new(),
        stray: None,
    };
    for &name in &CONCRETE[..1 + g.below(CONCRETE.len())] {
        let mut names = FIELDS.to_vec();
        let earlier = m.structs.len();
        let fields = (0..1 + g.below(2))
            .map(|_| {
                let field = names.remove(g.below(names.len()));
                let ty = if earlier > 0 && g.one_in(4) {
                    Ty::S(g.below(earlier), Vec::new())
                } else {
                    Ty::Str
                };
                (field, ty)
            })
            .collect();
        m.structs.push(StructDef {
            name,
            params: Vec::new(),
            fields,
        });
    }
    if g.one_in(2) {
        let mut fields = vec![("item", Ty::P(0))];
        if g.one_in(2) {
            fields.insert(g.below(2), ("label", Ty::Str));
        }
        m.structs.push(StructDef {
            name: "Box",
            params: vec!["T"],
            fields,
        });
    }
    if g.one_in(3) {
        m.structs.push(StructDef {
            name: "Pair",
            params: vec!["A", "B"],
            fields: vec![("left", Ty::P(0)), ("right", Ty::P(1))],
        });
    }
    if matches!(kind, Some(Kind::NoImpl | Kind::BoundUnmet | Kind::NotBuilt)) {
        m.stray = Some(m.structs.len());
        m.structs.push(StructDef {
            name: "Rock",
            params: Vec::new(),
            fields: vec![("a", Ty::Str)],
        });
    }

    // The order of the functions and traits: `main` first, then the rest
    // in any order, each calling only those after it.
    let traits = (1 + g.below(TRAITS.len())).max(if kind == Some(Kind::Clash) { 2 } else { 1 });
    let fns = 1 + g.below(3);
    let mut ranks: Vec<usize> = (1..=traits + fns).collect();
    let mut order = Vec::new();
    while !ranks.is_empty() {
        order.push(ranks.remove(g.below(ranks.len())));
    }
    for (k, &name) in TRAITS[..traits].iter().enumerate() {
        let out = g.one_in(3) || kind == Some(Kind::OutFixed) && k == 0;
        let params: Vec<&str> = if out { vec!["A", "B"] } else { vec!["T"] };
        let mut names = TRAIT_FNS.to_vec();
        let count = 1 + g.below(2);
        let fns = (0..count)
            .map(|i| {
                let name = if kind == Some(Kind::Clash) && k == 1 && i == 0 {
                    names.retain(|n| *n != m.traits[0].fns[0].name);
                    m.traits[0].fns[0].name
                } else {
                    names.remove(g.below(names.len()))
                };
                let mut params = vec![("x", Ty::P(0))];
                let mut default = None;
                if g.one_in(3) || kind == Some(Kind::ImplDefault) && k == 0 && i == 0 {
                    params.push(("note", Ty::Str));
                    default = g.one_in(2).then(|| {
                        let count = g.below(6);
                        (0..count).map(|_| g.below(PIECES.len())).collect()
                    });
                }
                let result = if out && i == 0 && kind == Some(Kind::OutFixed) {
                    Ty::P(1)
                } else if out {
                    g.pick(&[Ty::Str, Ty::P(1), Ty::P(0)]).clone()
                } else {
                    g.pick(&[Ty::Str, Ty::P(0)]).clone()
                };
                TraitFn {
                    name,
                    params,
                    default,
                    result,
                    console: g.one_in(3),
                }
            })
            .collect();
        m.traits.push(TraitDef {
            name,
            params,
            out,
            fns,
            rank: order[k],
        });
    }

    // The implementations: for text, for structs, and for every struct
    // with type parameters at once. Each trait has one for a type with
    // none, so `main` can call it.
    for k in 0..traits {
        let mut heads: Vec<Ty> = vec![Ty::Str];
        heads.extend(m.concrete().into_iter().map(|s| Ty::S(s, Vec::new())));
        let plain = heads.len();
        heads.extend(m.generic().into_iter().map(|s| {
            let n = m.structs[s].params.len();
            Ty::S(s, (0..n).map(Ty::P).collect())
        }));
        let mut chosen: Vec<usize> = (0..heads.len()).filter(|_| g.one_in(2)).collect();
        if !chosen.iter().any(|&h| h < plain) {
            chosen.insert(0, g.below(plain));
        }
        for h in chosen {
            let head = heads[h].clone();
            let generics: Vec<Generic> = match &head {
                Ty::S(s, args) if !args.is_empty() => (m.structs[*s].params.iter())
                    .map(|&p| {
                        let bound = g.one_in(3).then(|| {
                            let later: Vec<usize> = (0..traits)
                                .filter(|&j| j == k || order[j] > order[k])
                                .collect();
                            *g.pick(&later)
                        });
                        (p, bound)
                    })
                    .collect(),
                _ => Vec::new(),
            };
            let mut args = vec![head.clone()];
            if m.traits[k].out {
                args.push(if g.one_in(3) {
                    head
                } else {
                    some_type(g, &m, generics.len(), 1)
                });
            }
            m.impls.push(ImplDef {
                of: k,
                generics,
                args,
                bodies: Vec::new(),
                second: false,
            });
        }
    }

    m.fns.push(FnDef {
        name: "main".to_string(),
        rank: 0,
        generics: Vec::new(),
        params: Vec::new(),
        result: None,
        console: true,
        body: Vec::new(),
    });
    for i in 1..=fns {
        let rank = order[traits + i - 1];
        let later: Vec<usize> = (0..traits).filter(|&k| order[k] > rank).collect();
        let count = g.below(GENERICS.len() + 1);
        let generics: Vec<Generic> = GENERICS[..count]
            .iter()
            .map(|&name| {
                let bound = (!later.is_empty() && g.one_in(2)).then(|| *g.pick(&later));
                (name, bound)
            })
            .collect();
        let mut params: Vec<(&str, Ty)> = PARAMS[..1 + g.below(3)]
            .iter()
            .map(|&p| (p, some_type(g, &m, count, 1)))
            .collect();
        for p in 0..count {
            if !params.iter().any(|(_, t)| t.mentions(p)) {
                let at = p.min(params.len() - 1);
                params[at].1 = Ty::P(p);
            }
        }
        for p in 0..count {
            if !params.iter().any(|(_, t)| t.mentions(p)) {
                params.push((PARAMS[params.len()], Ty::P(p)));
            }
        }
        let result = (!g.one_in(3)).then(|| some_type(g, &m, count, 1));
        m.fns.push(FnDef {
            name: format!("f{i}"),
            rank,
            generics,
            params,
            result,
            console: g.one_in(2),
            body: Vec::new(),
        });
    }

    // The bodies, once every signature is known.
    for i in 0..m.fns.len() {
        let def = &m.fns[i];
        let ctx = Ctx {
            rank: def.rank,
            scope: def.generics.iter().map(|(_, b)| *b).collect(),
            names: (def.params.iter())
                .map(|(p, t)| (p.to_string(), t.clone()))
                .collect(),
            console: def.console,
            own: None,
        };
        let result = def.result.clone();
        let stmts = body(g, &m, ctx, result.as_ref());
        m.fns[i].body = stmts;
    }
    for i in 0..m.impls.len() {
        let bodies = impl_bodies(g, &m, i);
        m.impls[i].bodies = bodies;
    }

    // `main` calls each trait, and each function that nothing calls yet
    // where it can.
    let main = Ctx {
        rank: 0,
        scope: Vec::new(),
        names: Vec::new(),
        console: true,
        own: None,
    };
    let mut first = Vec::new();
    for k in 0..traits {
        let heads: Vec<Ty> = (m.impls.iter())
            .filter(|im| im.of == k && im.generics.is_empty())
            .map(|im| im.args[0].clone())
            .collect();
        let x = g.pick(&heads).clone();
        let (types, _) = m.holds(k, &x, &[]).expect("implemented");
        let f = &m.traits[k].fns[0];
        let params: Vec<Ty> = f.params.iter().map(|(_, t)| t.subst(&types)).collect();
        let gives = f.result.subst(&types);
        let call = Expr::TraitCall {
            of: k,
            func: 0,
            args: trait_args(g, &m, &main, (k, 0), &params, DEPTH),
            types,
        };
        first.push(use_of(call, &gives, first.len()));
    }
    for f in 1..m.fns.len() {
        let called = (m.fns.iter().flat_map(|d| &d.body))
            .chain(m.impls.iter().flat_map(|im| im.bodies.iter().flatten()))
            .chain(&first)
            .any(|s| calls(stmt_expr(s), f));
        if called {
            continue;
        }
        let def = &m.fns[f];
        let mut types = Vec::new();
        for (_, bound) in &def.generics {
            let options: Vec<Ty> = (candidates(&m, &main).into_iter())
                .filter(|t| bound.is_none_or(|b| m.meets(b, t, &[])))
                .collect();
            if options.is_empty() {
                break;
            }
            types.push(g.pick(&options).clone());
        }
        if types.len() < def.generics.len() {
            continue;
        }
        let params: Vec<Ty> = def.params.iter().map(|(_, t)| t.subst(&types)).collect();
        let gives = def.result.as_ref().map(|r| r.subst(&types));
        let call = Expr::Call {
            f,
            args: args(g, &m, &main, &params, DEPTH),
            types,
        };
        first.push(match gives {
            Some(gives) => use_of(call, &gives, first.len()),
            None => Stmt::Expr(call),
        });
    }
    first.append(&mut m.fns[0].body);
    m.fns[0].body = first;
    if kind == Some(Kind::SecondImpl) {
        let firsts: Vec<usize> = (0..m.impls.len())
            .filter(|&i| m.impls[i].generics.is_empty())
            .collect();
        let i = *g.pick(&firsts);
        let k = m.impls[i].of;
        let mut args = vec![m.impls[i].args[0].clone()];
        if m.traits[k].out {
            args.push(some_type(g, &m, 0, 1));
        }
        m.impls.push(ImplDef {
            of: k,
            generics: Vec::new(),
            args,
            bodies: Vec::new(),
            second: true,
        });
        let last = m.impls.len() - 1;
        let bodies = impl_bodies(g, &m, last);
        m.impls[last].bodies = bodies;
    }
    m
}

/// A statement that uses what a call in `main` gives.
fn use_of(call: Expr, gives: &Ty, n: usize) -> Stmt {
    if *gives == Ty::Str {
        Stmt::Expr(Expr::Print(Box::new(call)))
    } else {
        Stmt::Define {
            name: format!("y{n}"),
            ty: gives.clone(),
            typed: false,
            value: call,
        }
    }
}

fn impl_bodies(g: &mut Gen, m: &Model, i: usize) -> Vec<Vec<Stmt>> {
    let im = &m.impls[i];
    let t = &m.traits[im.of];
    (t.fns.iter())
        .map(|f| {
            let ctx = Ctx {
                rank: t.rank,
                scope: im.generics.iter().map(|(_, b)| *b).collect(),
                names: (f.params.iter())
                    .map(|(p, ty)| (p.to_string(), ty.subst(&im.args)))
                    .collect(),
                console: f.console,
                own: Some(im.of),
            };
            body(g, m, ctx, Some(&f.result.subst(&im.args)))
        })
        .collect()
}

fn stmt_expr(stmt: &Stmt) -> &Expr {
    match stmt {
        Stmt::Define { value, .. } | Stmt::Expr(value) => value,
    }
}

/// The expressions directly inside an expression.
fn inside(expr: &Expr) -> Vec<&Expr> {
    match expr {
        Expr::Text(_) | Expr::Name(_) => Vec::new(),
        Expr::Print(e) => vec![e],
        Expr::Call { args, .. } | Expr::TraitCall { args, .. } => {
            args.iter().map(|a| &a.value).collect()
        }
        Expr::Construct { fields, .. } => fields.iter().map(|(_, e)| e).collect(),
        Expr::Field { of, .. } => vec![of],
    }
}

fn calls(expr: &Expr, f: usize) -> bool {
    matches!(expr, Expr::Call { f: c, .. } if *c == f)
        || inside(expr).into_iter().any(|e| calls(e, f))
}

// The copies of functions the program runs.

/// A function with a body: one written on its own, or the function of an
/// implementation for the trait's function at a place.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Template {
    Fn(usize),
    Impl(usize, usize),
}

/// A copy of a function, for the types its type parameters stand for.
type Copy = (Template, Vec<Ty>);

impl Model {
    fn body_of(&self, t: Template) -> &[Stmt] {
        match t {
            Template::Fn(f) => &self.fns[f].body,
            Template::Impl(i, func) => &self.impls[i].bodies[func],
        }
    }

    fn params_of(&self, t: Template) -> Vec<&'static str> {
        match t {
            Template::Fn(f) => self.fns[f].params.iter().map(|(p, _)| *p).collect(),
            Template::Impl(i, func) => {
                let f = &self.traits[self.impls[i].of].fns[func];
                f.params.iter().map(|(p, _)| *p).collect()
            }
        }
    }

    /// The default of `note` for a call of a trait's function, if it has
    /// one.
    fn default_of(&self, call: &Expr) -> Option<&[usize]> {
        match call {
            Expr::TraitCall { of, func, .. } => self.traits[*of].fns[*func].default.as_deref(),
            _ => None,
        }
    }

    /// The copy a call runs, in the copy with types `env`.
    fn target(&self, call: &Expr, env: &[Ty]) -> Copy {
        match call {
            Expr::Call { f, types, .. } => {
                let types = types.iter().map(|t| t.subst(env)).collect();
                (Template::Fn(*f), types)
            }
            Expr::TraitCall {
                of, func, types, ..
            } => {
                let x = types[0].subst(env);
                let (_, by) = self.holds(*of, &x, &[]).expect("implemented");
                let (i, types) = by.expect("an implementation");
                (Template::Impl(i, *func), types)
            }
            _ => unreachable!("a call"),
        }
    }
}

/// A value: text, or a struct's fields in the order they are declared.
#[derive(Clone)]
enum Val {
    Text(Vec<u8>),
    Struct(Vec<Val>),
}

fn run(m: &Model, copy: &Copy, args: Vec<Val>, out: &mut Vec<u8>) -> Val {
    let (t, env) = copy;
    let mut names: Vec<(String, Val)> = (m.params_of(*t).into_iter())
        .map(str::to_string)
        .zip(args)
        .collect();
    let mut last = Val::Text(Vec::new());
    for stmt in m.body_of(*t) {
        match stmt {
            Stmt::Define { name, value, .. } => {
                let value = eval(m, env, &names, value, out);
                names.push((name.clone(), value));
            }
            Stmt::Expr(value) => last = eval(m, env, &names, value, out),
        }
    }
    last
}

fn eval(m: &Model, env: &[Ty], names: &[(String, Val)], expr: &Expr, out: &mut Vec<u8>) -> Val {
    match expr {
        Expr::Text(pieces) => Val::Text(pieces.iter().flat_map(|&p| PIECES[p].1.bytes()).collect()),
        Expr::Name(name) => names
            .iter()
            .rev()
            .find(|(n, _)| n == name)
            .unwrap()
            .1
            .clone(),
        Expr::Print(e) => {
            let Val::Text(text) = eval(m, env, names, e, out) else {
                unreachable!("`print` takes text")
            };
            out.extend_from_slice(&text);
            out.push(b'\n');
            Val::Text(Vec::new())
        }
        Expr::Call { args, .. } | Expr::TraitCall { args, .. } => {
            let copy = m.target(expr, env);
            let mut given = vec![None; m.params_of(copy.0).len()];
            for arg in args {
                given[arg.param] = Some(eval(m, env, names, &arg.value, out));
            }
            let values = (given.into_iter())
                .map(|value| {
                    value.unwrap_or_else(|| {
                        let pieces = m.default_of(expr).expect("a default");
                        Val::Text(pieces.iter().flat_map(|&p| PIECES[p].1.bytes()).collect())
                    })
                })
                .collect();
            run(m, &copy, values, out)
        }
        Expr::Construct { s, fields, .. } => {
            let mut values = vec![Val::Text(Vec::new()); m.structs[*s].fields.len()];
            for (field, value) in fields {
                values[*field] = eval(m, env, names, value, out);
            }
            Val::Struct(values)
        }
        Expr::Field { of, field, .. } => match eval(m, env, names, of, out) {
            Val::Struct(mut values) => values.swap_remove(*field),
            Val::Text(_) => unreachable!("a field is read from a struct"),
        },
    }
}

// Writing the program out with one function and one struct for each type
// used.

struct WrittenOut<'a> {
    m: &'a Model,
    copies: Vec<(Copy, String)>,
    structs: Vec<((usize, Vec<Ty>), String)>,
    text: String,
}

impl WrittenOut<'_> {
    fn copy_name(&mut self, copy: Copy) -> String {
        if let Some((_, name)) = self.copies.iter().find(|(c, _)| *c == copy) {
            return name.clone();
        }
        let base = match copy.0 {
            Template::Fn(f) => self.m.fns[f].name.clone(),
            Template::Impl(i, func) => self.m.traits[self.m.impls[i].of].fns[func].name.to_string(),
        };
        let name = match copy.0 {
            Template::Fn(f) if self.m.fns[f].generics.is_empty() => base,
            _ => format!("{base}_{}", self.copies.len()),
        };
        self.copies.push((copy, name.clone()));
        name
    }

    fn struct_name(&mut self, s: usize, types: &[Ty]) -> String {
        let key = (s, types.to_vec());
        if let Some((_, name)) = self.structs.iter().find(|(k, _)| *k == key) {
            return name.clone();
        }
        let base = self.m.structs[s].name;
        let name = if types.is_empty() {
            base.to_string()
        } else {
            format!("{base}_{}", self.structs.len())
        };
        self.structs.push((key, name.clone()));
        name
    }

    fn type_name(&mut self, ty: &Ty) -> String {
        match ty {
            Ty::Str => "str".to_string(),
            Ty::S(s, types) => self.struct_name(*s, types),
            Ty::P(_) => unreachable!("a type of a copy has no type parameter"),
        }
    }

    fn all(mut self, main: Copy) -> (String, usize) {
        self.copy_name(main);
        let mut i = 0;
        while i < self.copies.len() {
            let copy = self.copies[i].clone();
            self.function(&copy);
            i += 1;
        }
        let mut i = 0;
        while i < self.structs.len() {
            let ((s, types), name) = self.structs[i].clone();
            self.text.push_str(&format!("struct {name} {{\n"));
            for (field, ty) in &self.m.structs[s].fields {
                let ty = self.type_name(&ty.subst(&types));
                self.text.push_str(&format!("  {field}: {ty}\n"));
            }
            self.text.push_str("}\n\n");
            i += 1;
        }
        (self.text, self.copies.len())
    }

    fn function(&mut self, ((t, env), name): &(Copy, String)) {
        let m = self.m;
        let (params, result, console): (Vec<(&str, Ty)>, Option<Ty>, bool) = match *t {
            Template::Fn(f) => {
                let def = &m.fns[f];
                let params = def
                    .params
                    .iter()
                    .map(|(p, ty)| (*p, ty.subst(env)))
                    .collect();
                (
                    params,
                    def.result.as_ref().map(|r| r.subst(env)),
                    def.console,
                )
            }
            Template::Impl(i, func) => {
                let im = &m.impls[i];
                let f = &m.traits[im.of].fns[func];
                let params = (f.params.iter())
                    .map(|(p, ty)| (*p, ty.subst(&im.args).subst(env)))
                    .collect();
                (params, Some(f.result.subst(&im.args).subst(env)), f.console)
            }
        };
        let params: Vec<String> = (params.iter())
            .map(|(p, ty)| format!("{p}: {}", self.type_name(ty)))
            .collect();
        let mut text = format!("fn {name}({})", params.join(", "));
        if let Some(r) = &result {
            text.push_str(&format!(" -> {}", self.type_name(r)));
        }
        if console {
            text.push_str(" ! console");
        }
        text.push_str(" {\n");
        for stmt in m.body_of(*t) {
            text.push_str("  ");
            match stmt {
                Stmt::Define {
                    name,
                    ty,
                    typed,
                    value,
                } => {
                    text.push_str(name);
                    if *typed {
                        text.push_str(&format!(": {}", self.type_name(&ty.subst(env))));
                    }
                    text.push_str(" = ");
                    text.push_str(&self.expr(value, env));
                }
                Stmt::Expr(value) => text.push_str(&self.expr(value, env)),
            }
            text.push('\n');
        }
        text.push_str("}\n\n");
        self.text.push_str(&text);
    }

    fn expr(&mut self, expr: &Expr, env: &[Ty]) -> String {
        let m = self.m;
        match expr {
            Expr::Text(pieces) => {
                let pieces: String = pieces.iter().map(|&p| PIECES[p].0).collect();
                format!("\"{pieces}\"")
            }
            Expr::Name(name) => name.clone(),
            Expr::Print(e) => format!("print({})", self.expr(e, env)),
            Expr::Call { args, .. } | Expr::TraitCall { args, .. } => {
                let copy = m.target(expr, env);
                let params = m.params_of(copy.0);
                let name = self.copy_name(copy);
                let mut args: Vec<String> = (args.iter())
                    .map(|a| {
                        let value = self.expr(&a.value, env);
                        if a.named {
                            format!("{}: {value}", params[a.param])
                        } else {
                            value
                        }
                    })
                    .collect();
                // A default left out is written after the rest.
                if args.len() < params.len()
                    && let Some(pieces) = m.default_of(expr)
                {
                    let text: String = pieces.iter().map(|&p| PIECES[p].0).collect();
                    args.push(format!("note: \"{text}\""));
                }
                format!("{name}({})", args.join(", "))
            }
            Expr::Construct { s, types, fields } => {
                let types: Vec<Ty> = types.iter().map(|t| t.subst(env)).collect();
                let name = self.struct_name(*s, &types);
                let fields: Vec<String> = (fields.iter())
                    .map(|(f, e)| format!("{}: {}", m.structs[*s].fields[*f].0, self.expr(e, env)))
                    .collect();
                format!("{name}({})", fields.join(", "))
            }
            Expr::Field { of, s, field } => {
                format!("{}.{}", self.expr(of, env), m.structs[*s].fields[*field].0)
            }
        }
    }
}

// Writing the model out as source.

/// The source being built, and the mistake once it is placed.
struct Builder<'a> {
    g: &'a mut Gen,
    kind: Option<Kind>,
    text: String,
    mistake: Option<Expected>,
    /// Which call of a trait's function, written with its trait in a body
    /// `main` reaches, the mistake goes at, and how many are written.
    clash_site: usize,
    clashes: usize,
    /// A function `main` calls, written at the end of the file, whose type
    /// parameter has this bound, or the mistake for a bound if `None`.
    spare: Option<Option<&'static str>>,
    /// A definition written at the end of the file, as the text before the
    /// mistake and the rest.
    last: Option<(&'static str, &'static str)>,
    /// A parameter written under a new name in the body being written, as
    /// the name the model gives it and the name the program does.
    renamed: Option<(String, String)>,
}

impl Builder<'_> {
    fn push(&mut self, text: &str) {
        self.text.push_str(text);
    }

    fn mark(&mut self, mentions: impl Into<String>, fix: Option<String>) {
        assert!(self.mistake.is_none(), "one mistake per file");
        self.mistake = Some(Expected {
            at: pos_after(&self.text),
            mentions: mentions.into(),
            fix,
        });
    }
}

/// How a type is written among the type parameters `names`.
fn type_text(m: &Model, ty: &Ty, names: &[&str]) -> String {
    match ty {
        Ty::Str => "str".to_string(),
        Ty::P(p) => names[*p].to_string(),
        Ty::S(s, args) => {
            let name = m.structs[*s].name;
            if args.is_empty() {
                name.to_string()
            } else {
                let args: Vec<String> = args.iter().map(|t| type_text(m, t, names)).collect();
                format!("{name}[{}]", args.join(", "))
            }
        }
    }
}

fn generics_text(m: &Model, generics: &[Generic]) -> String {
    if generics.is_empty() {
        return String::new();
    }
    let each: Vec<String> = (generics.iter())
        .map(|(name, bound)| match bound {
            Some(k) => format!("{name}: {}", m.traits[*k].name),
            None => name.to_string(),
        })
        .collect();
    format!("[{}]", each.join(", "))
}

/// A definition of the program, by its place.
#[derive(Clone, Copy)]
enum Item {
    Struct(usize),
    Trait(usize),
    Impl(usize),
    Fn(usize),
}

/// The places of the mistake, chosen before writing.
struct Sites {
    /// The implementation the mistake goes in, and its trait's function.
    impl_site: Option<(usize, usize)>,
    /// The same, for a default written in an implementation.
    default_site: Option<(usize, usize)>,
    /// The function whose first type parameter's bound is the mistake.
    bound_site: Option<usize>,
}

fn build(g: &mut Gen, kind: Option<Kind>) -> TraitCase {
    let m = model(g, kind);
    let (written_out, copies, stdout) = if kind.is_none() {
        let main = (Template::Fn(0), Vec::new());
        let mut stdout = Vec::new();
        run(&m, &main, Vec::new(), &mut stdout);
        let out = WrittenOut {
            m: &m,
            copies: Vec::new(),
            structs: Vec::new(),
            text: String::new(),
        };
        let (text, copies) = out.all(main);
        (text, copies, stdout)
    } else {
        (String::new(), 0, Vec::new())
    };

    let reached = reached_fns(&m);
    let firsts: Vec<(usize, usize)> = (0..m.impls.len())
        .filter(|&i| !m.impls[i].second)
        .flat_map(|i| (0..m.traits[m.impls[i].of].fns.len()).map(move |f| (i, f)))
        .collect();
    let sites = Sites {
        impl_site: matches!(
            kind,
            Some(Kind::LacksFn | Kind::ExtraFn | Kind::SignatureDiffers)
        )
        .then(|| *g.pick(&firsts)),
        default_site: (kind == Some(Kind::ImplDefault)).then(|| {
            let of_first: Vec<usize> = (0..m.impls.len())
                .filter(|&i| m.impls[i].of == 0 && !m.impls[i].second)
                .collect();
            (*g.pick(&of_first), 0)
        }),
        bound_site: (kind == Some(Kind::BoundNotTrait))
            .then(|| {
                let with: Vec<usize> = (reached.iter().copied())
                    .filter(|&f| !m.fns[f].generics.is_empty())
                    .collect();
                (!with.is_empty()).then(|| *g.pick(&with))
            })
            .flatten(),
    };
    let qualified: usize = (m.fns.iter().enumerate())
        .filter(|(f, _)| reached.contains(f))
        .flat_map(|(_, d)| &d.body)
        .chain(m.impls.iter().flat_map(|im| im.bodies.iter().flatten()))
        .map(|s| qualified_in(&m, stmt_expr(s)))
        .sum();
    let mut b = Builder {
        clash_site: g.below(qualified.max(1)),
        g,
        kind,
        text: String::new(),
        mistake: None,
        clashes: 0,
        spare: None,
        last: None,
        renamed: None,
    };

    let mut items: Vec<Item> = Vec::new();
    items.extend((0..m.structs.len()).map(Item::Struct));
    items.extend((0..m.traits.len()).map(Item::Trait));
    items.extend(
        (0..m.impls.len())
            .filter(|&i| !m.impls[i].second)
            .map(Item::Impl),
    );
    items.extend((0..m.fns.len()).map(Item::Fn));
    let mut order = Vec::new();
    while !items.is_empty() {
        order.push(items.remove(b.g.below(items.len())));
    }
    // A second implementation is written after the first.
    order.extend(
        (0..m.impls.len())
            .filter(|&i| m.impls[i].second)
            .map(Item::Impl),
    );
    for (n, item) in order.into_iter().enumerate() {
        if n > 0 {
            b.push("\n");
        }
        match item {
            Item::Struct(s) => structure(&mut b, &m, s),
            Item::Trait(k) => declared_trait(&mut b, &m, k),
            Item::Impl(i) => implementation(&mut b, &m, i, &sites),
            Item::Fn(f) => function(&mut b, &m, f, reached.contains(&f), &sites),
        }
    }
    if let Some(bound) = b.spare {
        b.push("\nfn spare[T: ");
        match bound {
            Some(name) => b.push(name),
            None => bad_bound(&mut b, &m),
        }
        b.push("](a: T) -> str { \"a\" }\n");
    }
    if let Some((before, rest)) = b.last {
        b.push("\n");
        b.push(before);
        b.mark(NOT_BUILT, None);
        b.push(rest);
        b.push("\n");
    }
    assert!(
        kind.is_none() || b.mistake.is_some(),
        "the mistake {kind:?} was placed"
    );
    // The `use` lines of what the program writes of the library come first,
    // and the mistake is placed after them.
    let source = with_imports(&b.text);
    let by = lines_added(&b.text, &source);
    let mistake = b.mistake.map(|mut m| {
        m.at = shifted(m.at, by);
        m
    });
    TraitCase {
        source,
        written_out: with_imports(&written_out),
        stdout,
        copies,
        mistake,
    }
}

/// The functions `main` reaches, as the compiler finds them: through the
/// calls of each function it reaches and of every implementation, as each
/// trait is called from `main`.
fn reached_fns(m: &Model) -> Vec<usize> {
    let mut reached = vec![0];
    for im in &m.impls {
        for s in im.bodies.iter().flatten() {
            for f in 1..m.fns.len() {
                if calls(stmt_expr(s), f) && !reached.contains(&f) {
                    reached.push(f);
                }
            }
        }
    }
    let mut i = 0;
    while i < reached.len() {
        for s in &m.fns[reached[i]].body {
            for f in 1..m.fns.len() {
                if calls(stmt_expr(s), f) && !reached.contains(&f) {
                    reached.push(f);
                }
            }
        }
        i += 1;
    }
    reached
}

fn qualified_in(m: &Model, expr: &Expr) -> usize {
    let here = matches!(expr, Expr::TraitCall { of, func, .. } if m.clashes(m.traits[*of].fns[*func].name));
    usize::from(here)
        + inside(expr)
            .into_iter()
            .map(|e| qualified_in(m, e))
            .sum::<usize>()
}

fn structure(b: &mut Builder, m: &Model, s: usize) {
    let def = &m.structs[s];
    b.push(&format!("struct {}", def.name));
    if !def.params.is_empty() {
        b.push(&format!("[{}]", def.params.join(", ")));
    }
    b.push(" {\n");
    for (field, ty) in &def.fields {
        b.push(&format!("  {field}: {}\n", type_text(m, ty, &def.params)));
    }
    b.push("}\n");
}

/// A trait function's signature, among the types `args`, which are among
/// the type parameters `names`.
fn trait_fn_text(m: &Model, f: &TraitFn, args: &[Ty], names: &[&str]) -> String {
    let params: Vec<String> = (f.params.iter())
        .map(|(p, t)| format!("{p}: {}", type_text(m, &t.subst(args), names)))
        .collect();
    let mut text = format!(
        "fn {}({}) -> {}",
        f.name,
        params.join(", "),
        type_text(m, &f.result.subst(args), names)
    );
    if f.console {
        text.push_str(" ! console");
    }
    text
}

fn declared_trait(b: &mut Builder, m: &Model, k: usize) {
    let t = &m.traits[k];
    let params = if t.out { "A, out B" } else { "T" };
    b.push(&format!("trait {}[{params}] {{\n", t.name));
    let own: Vec<Ty> = (0..t.params.len()).map(Ty::P).collect();
    for f in &t.fns {
        let mut text = trait_fn_text(m, f, &own, &t.params);
        if let Some(pieces) = &f.default {
            let literal: String = pieces.iter().map(|&p| PIECES[p].0).collect();
            text = text.replace("note: str", &format!("note: str = \"{literal}\""));
        }
        b.push(&format!("  {text}\n"));
    }
    b.push("}\n");
}

fn implementation(b: &mut Builder, m: &Model, i: usize, sites: &Sites) {
    let im = &m.impls[i];
    let t = &m.traits[im.of];
    let names: Vec<&str> = im.generics.iter().map(|(n, _)| *n).collect();
    b.push("impl");
    b.push(&generics_text(m, &im.generics));
    b.push(" ");
    let site = sites.impl_site.filter(|(at, _)| *at == i).map(|(_, f)| f);
    let args: Vec<String> = im.args.iter().map(|a| type_text(m, a, &names)).collect();
    if im.second {
        b.mark("already implemented", None);
    }
    if let (Some(f), Some(Kind::LacksFn)) = (site, b.kind) {
        b.mark(format!("`{}`", t.fns[f].name), None);
    }
    b.push(&format!("{}[{}]", t.name, args.join(", ")));
    if names.is_empty() {
        b.push(" {\n");
    } else {
        for name in &names {
            b.push(&format!("\n  requires Copy[{name}]\n  requires Discard[{name}]"));
        }
        b.push("\n{\n");
    }
    let ctx_names: Vec<&str> = names.clone();
    for (func, f) in t.fns.iter().enumerate() {
        let here = site == Some(func);
        if here && b.kind == Some(Kind::LacksFn) {
            continue;
        }
        b.push("  ");
        let wanted = trait_fn_text(m, f, &im.args, &ctx_names);
        let repeats = sites.default_site == Some((i, func));
        if repeats {
            let (before, after) = wanted.split_once("note: str").expect("a `note`");
            b.push(&format!("{before}note: str = "));
            b.mark(format!("`{}`", t.name), Some("remove".to_string()));
            b.push(&format!("\"a\"{after}"));
        } else if here && b.kind == Some(Kind::SignatureDiffers) {
            b.push("fn ");
            b.mark(format!("`{}`", f.name), Some(format!("`{wanted}`")));
            let (text, renamed) = differing(b.g, m, f, &im.args, &ctx_names);
            b.renamed = renamed;
            b.push(&text);
        } else {
            b.push(&wanted);
        }
        b.push(" {\n");
        statements(b, m, &im.bodies[func], &ctx_names, "    ", true);
        b.renamed = None;
        b.push("  }\n");
        if here && b.kind == Some(Kind::ExtraFn) {
            b.push("  ");
            let name = *b.g.pick(&["extra", "spare", "other"]);
            b.push("fn ");
            b.mark(format!("`{name}`"), None);
            b.push(&format!("{name}(x: str) -> str {{ x }}\n"));
        }
    }
    b.push("}\n");
}

/// A signature for trait function `f` that differs from the trait's in one
/// way, and the parameter it renames, if that is the way.
fn differing(
    g: &mut Gen,
    m: &Model,
    f: &TraitFn,
    args: &[Ty],
    names: &[&str],
) -> (String, Option<(String, String)>) {
    let mut params: Vec<(String, String)> = (f.params.iter())
        .map(|(p, t)| (p.to_string(), type_text(m, &t.subst(args), names)))
        .collect();
    let mut result = type_text(m, &f.result.subst(args), names);
    let mut console = f.console;
    let other = |t: &str| {
        if t == "str" {
            m.structs[0].name.to_string()
        } else {
            "str".to_string()
        }
    };
    let mut renamed = None;
    match g.below(5) {
        0 => params[0].1 = other(&params[0].1),
        1 => {
            renamed = Some((params[0].0.clone(), "y".to_string()));
            params[0].0 = "y".to_string();
        }
        2 => result = other(&result),
        3 => console = !console,
        _ => params.push(("extra".to_string(), "str".to_string())),
    }
    let params: Vec<String> = params.iter().map(|(p, t)| format!("{p}: {t}")).collect();
    let mut text = format!("{}({}) -> {result}", f.name, params.join(", "));
    if console {
        text.push_str(" ! console");
    }
    (text, renamed)
}

/// Writes a bound that names something other than a trait.
fn bad_bound(b: &mut Builder, m: &Model) {
    let mut options = vec!["str".to_string(), "Nmaed".to_string()];
    options.extend(m.structs.iter().map(|s| s.name.to_string()));
    let name = b.g.pick(&options).clone();
    b.mark(format!("`{name}`"), None);
    b.push(&name);
}

fn function(b: &mut Builder, m: &Model, f: usize, reached: bool, sites: &Sites) {
    let def = &m.fns[f];
    let names: Vec<&str> = def.generics.iter().map(|(n, _)| *n).collect();
    b.push(&format!("fn {}", def.name));
    if sites.bound_site == Some(f) {
        let mut rest = def.generics.clone();
        let (name, _) = rest.remove(0);
        b.push(&format!("[{name}: "));
        bad_bound(b, m);
        for (n, bound) in rest {
            match bound {
                Some(k) => b.push(&format!(", {n}: {}", m.traits[k].name)),
                None => b.push(&format!(", {n}")),
            }
        }
        b.push("]");
    } else {
        b.push(&generics_text(m, &def.generics));
    }
    let params: Vec<String> = (def.params.iter())
        .map(|(p, t)| format!("{p}: {}", type_text(m, t, &names)))
        .collect();
    b.push(&format!("({})", params.join(", ")));
    if let Some(r) = &def.result {
        b.push(&format!(" -> {}", type_text(m, r, &names)));
    }
    if def.console {
        b.push(" ! console");
    }
    // Nothing is known of a type parameter but what its bounds say, so the
    // body may copy, drop and return its values only when each is `Copy` and `Discard`.
    if def.generics.is_empty() {
        b.push(" {\n");
    } else {
        for name in &names {
            b.push(&format!("\n  requires Copy[{name}]\n  requires Discard[{name}]"));
        }
        b.push("\n{\n");
    }
    statements(b, m, &def.body, &names, "  ", reached);
    if f == 0 {
        main_statement(b, m, sites);
    }
    b.push("}\n");
}

/// The statement at the end of `main` that holds the mistake, if it goes
/// there.
fn main_statement(b: &mut Builder, m: &Model, sites: &Sites) {
    let main = Ctx {
        rank: 0,
        scope: Vec::new(),
        names: Vec::new(),
        console: true,
        own: None,
    };
    match b.kind {
        Some(Kind::NoImpl) => {
            let rock = Ty::S(m.stray.expect("a stray struct"), Vec::new());
            let k = b.g.below(m.traits.len());
            let t = &m.traits[k];
            let f = &t.fns[0];
            let types = vec![rock; t.params.len()];
            let params: Vec<Ty> = f.params.iter().map(|(_, ty)| ty.subst(&types)).collect();
            let call = Expr::TraitCall {
                of: k,
                func: 0,
                args: args(b.g, m, &main, &params, DEPTH),
                types,
            };
            b.push("  wrong = ");
            b.mark(format!("`{}` for `Rock`", t.name), None);
            expr(b, m, &call, &[], false);
            b.push("\n");
        }
        Some(Kind::BoundUnmet) => {
            let rock = Ty::S(m.stray.expect("a stray struct"), Vec::new());
            // A function with a bound that `Rock` fails, whose other
            // bounds some type meets.
            let mut options = Vec::new();
            for f in 1..m.fns.len() {
                let def = &m.fns[f];
                for (p, (_, bound)) in def.generics.iter().enumerate() {
                    let Some(k) = bound else {
                        continue;
                    };
                    let mut types = Vec::new();
                    for (q, (_, other)) in def.generics.iter().enumerate() {
                        let t = if q == p {
                            Some(rock.clone())
                        } else {
                            (candidates(m, &main).into_iter())
                                .find(|t| other.is_none_or(|o| m.meets(o, t, &[])))
                        };
                        types.extend(t);
                    }
                    if types.len() == def.generics.len() {
                        options.push((f, *k, types));
                    }
                }
            }
            let Some((f, k, types)) = pick_some(b.g, &options).cloned() else {
                let k = b.g.below(m.traits.len());
                b.push("  wrong = ");
                b.mark(format!("`{}` for `Rock`", m.traits[k].name), None);
                b.push("spare(Rock(a: \"a\"))\n");
                b.spare = Some(Some(m.traits[k].name));
                return;
            };
            let params: Vec<Ty> = m.fns[f]
                .params
                .iter()
                .map(|(_, t)| t.subst(&types))
                .collect();
            let call = Expr::Call {
                f,
                args: args(b.g, m, &main, &params, DEPTH),
                types,
            };
            b.push(if m.fns[f].result.is_some() {
                "  wrong = "
            } else {
                "  "
            });
            b.mark(format!("`{}` for `Rock`", m.traits[k].name), None);
            expr(b, m, &call, &[], false);
            b.push("\n");
        }
        Some(Kind::NotBuilt) => {
            // `Rock` is reached, so what is written for it is checked.
            b.push("  wrong = Rock(a: \"a\")\n");
            b.last = Some(*b.g.pick(&[
                ("", "derive Named for Rock"),
                ("", "without Add for Rock"),
                ("", "effect Kind {\n  fn kind()\n}"),
                (
                    "trait Spare[T] {\n  fn spare_of(x: T, note: str = ",
                    "1.5) -> str\n}",
                ),
                ("impl ", "Copy[Rock] {}"),
                ("impl ", "Discard[Rock] {}"),
                ("trait ", "Copy[T] {\n  fn copy(x: T) -> T\n}"),
            ]));
            if b.last.is_some_and(|(before, _)| before.starts_with("fn")) {
                b.push("  print(spare(\"b\"))\n");
            }
        }
        Some(Kind::BoundNotTrait) if sites.bound_site.is_none() => {
            b.push("  print(spare(\"a\"))\n");
            b.spare = Some(None);
        }
        Some(Kind::OutFixed) => {
            let t = &m.traits[0];
            let heads: Vec<Ty> = (m.impls.iter())
                .filter(|im| im.of == 0 && im.generics.is_empty() && !im.second)
                .map(|im| im.args[0].clone())
                .collect();
            let x = b.g.pick(&heads).clone();
            let (types, _) = m.holds(0, &x, &[]).expect("implemented");
            let f = &t.fns[0];
            let params: Vec<Ty> = f.params.iter().map(|(_, ty)| ty.subst(&types)).collect();
            let gives = f.result.subst(&types);
            let call = Expr::TraitCall {
                of: 0,
                func: 0,
                args: args(b.g, m, &main, &params, DEPTH),
                types,
            };
            let (stated, mentions) = if gives == Ty::Str {
                (m.structs[0].name.to_string(), "gives text".to_string())
            } else {
                (
                    "str".to_string(),
                    format!("a `{}`", type_text(m, &gives, &[])),
                )
            };
            b.push(&format!("  wrong: {stated} = "));
            b.mark(mentions, None);
            expr(b, m, &call, &[], false);
            b.push("\n");
        }
        _ => {}
    }
}

fn statements(
    b: &mut Builder,
    m: &Model,
    stmts: &[Stmt],
    names: &[&str],
    indent: &str,
    reached: bool,
) {
    for stmt in stmts {
        b.push(indent);
        match stmt {
            Stmt::Define {
                name,
                ty,
                typed,
                value,
            } => {
                b.push(name);
                if *typed {
                    b.push(&format!(": {}", type_text(m, ty, names)));
                }
                b.push(" = ");
                expr(b, m, value, names, reached);
            }
            Stmt::Expr(value) => expr(b, m, value, names, reached),
        }
        b.push("\n");
    }
}

/// Writes an expression in a body that `main` reaches if `reached`.
fn expr(b: &mut Builder, m: &Model, e: &Expr, names: &[&str], reached: bool) {
    match e {
        Expr::Text(pieces) => {
            let pieces: String = pieces.iter().map(|&p| PIECES[p].0).collect();
            b.push(&format!("\"{pieces}\""));
        }
        Expr::Name(name) => match &b.renamed {
            Some((from, to)) if from == name => {
                let to = to.clone();
                b.push(&to);
            }
            _ => b.push(name),
        },
        Expr::Print(e) => {
            b.push("print(");
            expr(b, m, e, names, reached);
            b.push(")");
        }
        Expr::Call { f, args, .. } => {
            b.push(&m.fns[*f].name);
            call_args(
                b,
                m,
                args,
                &m.fns[*f].params.iter().map(|(p, _)| *p).collect::<Vec<_>>(),
                names,
                reached,
            );
        }
        Expr::TraitCall { of, func, args, .. } => {
            let t = &m.traits[*of];
            let f = &t.fns[*func];
            if m.clashes(f.name) {
                let site = reached && b.clashes == b.clash_site && b.kind == Some(Kind::Clash);
                if reached {
                    b.clashes += 1;
                }
                if site {
                    b.mark(
                        format!("`{}`", f.name),
                        Some(format!("`{}.{}`", t.name, f.name)),
                    );
                } else {
                    b.push(&format!("{}.", t.name));
                }
            }
            b.push(f.name);
            let params: Vec<&str> = f.params.iter().map(|(p, _)| *p).collect();
            call_args(b, m, args, &params, names, reached);
        }
        Expr::Construct { s, fields, .. } => {
            let def = &m.structs[*s];
            b.push(def.name);
            b.push("(");
            for (i, (field, value)) in fields.iter().enumerate() {
                if i > 0 {
                    b.push(", ");
                }
                b.push(&format!("{}: ", def.fields[*field].0));
                expr(b, m, value, names, reached);
            }
            b.push(")");
        }
        Expr::Field { of, s, field } => {
            expr(b, m, of, names, reached);
            b.push(&format!(".{}", m.structs[*s].fields[*field].0));
        }
    }
}

fn call_args(
    b: &mut Builder,
    m: &Model,
    args: &[Arg],
    params: &[&str],
    names: &[&str],
    reached: bool,
) {
    b.push("(");
    for (i, arg) in args.iter().enumerate() {
        if i > 0 {
            b.push(", ");
        }
        if arg.named {
            b.push(&format!("{}: ", params[arg.param]));
        }
        expr(b, m, &arg.value, names, reached);
    }
    b.push(")");
}

/// A program in which an implementation with a type parameter applies to
/// each type that fits: it prints the name of values wrapped in boxes to
/// any depth, around a struct or text, and what it prints.
pub fn boxes(g: &mut Gen) -> (String, Vec<u8>) {
    let labelled = g.one_in(2);
    let mut source = String::from(
        "use std.console (console, print)\n\nstruct Box[T] {\n  item: T\n  label: str\n}\n\nstruct Cat {\n  name: str\n}\n\n\
         trait Named[T] {\n  fn name_of(x: T) -> str\n}\n\n\
         impl Named[Cat] {\n  fn name_of(x: Cat) -> str { x.name }\n}\n\n\
         impl Named[str] {\n  fn name_of(x: str) -> str { x }\n}\n\n",
    );
    source.push_str(if labelled {
        "impl[T: Named] Named[Box[T]] {\n  fn name_of(x: Box[T]) -> str { x.label }\n}\n\n"
    } else {
        "impl[T: Named] Named[Box[T]] {\n  fn name_of(x: Box[T]) -> str { name_of(x.item) }\n}\n\n"
    });
    source.push_str("fn main() ! console {\n");
    let mut stdout = Vec::new();
    for _ in 0..1 + g.below(4) {
        let depth = g.below(4);
        let pieces: Vec<usize> = (0..g.below(3)).map(|_| g.below(PIECES.len())).collect();
        let inner: String = pieces.iter().map(|&p| PIECES[p].0).collect();
        let mut printed: String = pieces.iter().map(|&p| PIECES[p].1).collect();
        let mut value = if g.one_in(2) {
            format!("Cat(name: \"{inner}\")")
        } else {
            format!("\"{inner}\"")
        };
        for d in 0..depth {
            let label = format!("box{d}");
            value = format!("Box(item: {value}, label: \"{label}\")");
            if labelled {
                printed = label;
            }
        }
        source.push_str(&format!("  print(name_of({value}))\n"));
        stdout.extend_from_slice(printed.as_bytes());
        stdout.push(b'\n');
    }
    source.push_str("}\n");
    (source, stdout)
}

/// A program whose function requires an implementation of a trait of two
/// types, which one call uses, with what it must give.
pub struct Requires {
    pub source: String,
    /// Where the call is, and the names the error for it must mention, if
    /// the call is not one an implementation allows.
    pub mistake: Option<(Pos, Vec<String>)>,
    pub stdout: Vec<u8>,
}

impl std::fmt::Debug for Requires {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.source)
    }
}

/// `Pair` is implemented for some choices of two types directly. When
/// `through_show`, it is also implemented for a type and text, wherever the
/// type has `Show`, by an `impl` that requires it. A call of a function that
/// requires `Pair[A, B]` uses one choice of types.
pub fn requires(g: &mut Gen) -> Requires {
    const TYPES: [(&str, &str); 3] = [
        ("str", "\"s\""),
        ("Cat", "Cat(name: \"c\")"),
        ("Int", "one()"),
    ];
    let through_show = g.one_in(2);
    let shown: Vec<bool> = TYPES.iter().map(|_| g.one_in(2)).collect();
    let mut source = String::from(
        "use std.console (console, print)\n\nstruct Cat {\n  name: str\n}\n\nfn one() -> Int { 1 }\n\n\
         trait Show[T] {\n  fn show(x: T) -> str\n}\n\n\
         trait Pair[A, B] {\n  fn join(a: A, b: B) -> str\n}\n\n",
    );
    for (i, (name, _)) in TYPES.iter().enumerate() {
        if shown[i] {
            source.push_str(&format!(
                "impl Show[{name}] {{\n  fn show(x: {name}) -> str {{ \"{name}\" }}\n}}\n\n"
            ));
        }
    }
    if through_show {
        source.push_str(
            "impl[T] Pair[T, str]\n  requires Show[T]\n{\n  \
             fn join(a: T, b: str) -> str { show(a) }\n}\n\n",
        );
    }
    let mut direct = Vec::new();
    for (a, (x, _)) in TYPES.iter().enumerate() {
        for (b, (y, _)) in TYPES.iter().enumerate() {
            // A direct implementation never fits what the other fits.
            let overlaps = through_show && *y == "str";
            if !overlaps && g.one_in(3) {
                direct.push((a, b));
                source.push_str(&format!(
                    "impl Pair[{x}, {y}] {{\n  fn join(a: {x}, b: {y}) -> str {{ \"{x}{y}\" }}\n}}\n\n"
                ));
            }
        }
    }
    source.push_str(
        "fn show_both[A, B](a: A, b: B) -> str\n  requires Pair[A, B]\n{\n  join(a, b)\n}\n\n",
    );
    let (a, b) = (g.below(TYPES.len()), g.below(TYPES.len()));
    source.push_str("fn main() ! console {\n");
    let line = source.lines().count() + 1;
    source.push_str(&format!(
        "  print(show_both({}, {}))\n}}\n",
        TYPES[a].1, TYPES[b].1
    ));
    let shown_by = |t: usize| through_show && TYPES[b].0 == "str" && shown[t];
    let stdout = if direct.contains(&(a, b)) {
        Some(format!("{}{}", TYPES[a].0, TYPES[b].0))
    } else if shown_by(a) {
        Some(TYPES[a].0.to_string())
    } else {
        None
    };
    Requires {
        source,
        mistake: match stdout {
            Some(_) => None,
            None => Some((
                Pos { line, col: 9 },
                vec![
                    "Pair".to_string(),
                    format!("`{}`", TYPES[a].0),
                    format!("`{}`", TYPES[b].0),
                ],
            )),
        },
        stdout: stdout.map(|s| s + "\n").unwrap_or_default().into_bytes(),
    }
}

/// The program written with `requires Trait[T]` for each bound `T: Trait`
/// of its functions and implementations.
pub fn requires_for_bounds(source: &str) -> String {
    let mut out = String::new();
    for line in source.lines() {
        let generic = line.starts_with("impl[")
            || (line.starts_with("fn ") && line.split('(').next().is_some_and(|h| h.contains('[')));
        let (Some(open), true) = (line.find('['), generic) else {
            out.push_str(line);
            out.push('\n');
            continue;
        };
        let close = line[open..].find(']').expect("generics end") + open;
        if !line[open..close].contains(": ") {
            out.push_str(line);
            out.push('\n');
            continue;
        }
        let mut plain = Vec::new();
        let mut needs = Vec::new();
        for each in line[open + 1..close].split(", ") {
            match each.split_once(": ") {
                Some((name, bound)) => {
                    plain.push(name.to_string());
                    needs.push(format!("  requires {bound}[{name}]\n"));
                }
                None => plain.push(each.to_string()),
            }
        }
        out.push_str(&line[..open]);
        out.push_str(&format!("[{}]", plain.join(", ")));
        match line.strip_suffix(" {") {
            Some(head) => {
                out.push_str(&head[close + 1..]);
                out.push('\n');
                out.push_str(&needs.concat());
                out.push_str("{\n");
            }
            // The lines that follow, which end in the brace, are `requires`.
            None => {
                out.push_str(&line[close + 1..]);
                out.push('\n');
                out.push_str(&needs.concat());
            }
        }
    }
    out
}
