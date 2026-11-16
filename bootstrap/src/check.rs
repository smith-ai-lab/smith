//! Checks a parsed program against the rules of the language and lowers it
//! to the program every target receives.
//!
//! It collects every signature, then checks each definition `main`
//! reaches: the body of each function, each struct and trait those name,
//! and every implementation of those traits or for those structs. A
//! definition that `main` does not reach is parsed and not checked.
//!
//! It also finds what `smith check` warns of: each definition `main` does
//! not reach, and each binding whose name is never read.
//!
//! A function with type parameters is checked once, with its parameters
//! standing for any type that meets their bounds. It is lowered once for
//! each choice of types a call makes, and a call of a trait's function
//! becomes a call of the implementation the types choose, so no trait is
//! left when the program runs. A struct is lowered to its fields, one after
//! another, until only texts are left, so the form every target receives
//! holds texts alone.
//!
//! A default is filled in where the call or construction that leaves it out
//! is written, so it is lowered as if it were written there, and no copy of
//! a function knows which of its arguments were left out.

use std::collections::HashMap;

use crate::diag::{Error, Pos};
use crate::ir::{self, Step, Stream, Value};
use crate::parser::{Arg, Call, Expr, File, Function, Generic, Stmt, Type};

/// The effects the standard library declares, other than `console`.
const LIBRARY_EFFECTS: &[&str] = &["fs", "net", "time", "ui"];

/// The effects built into the compiler.
const BUILT_IN_EFFECTS: &[&str] = &["alloc", "spawn", "gpu", "diverge", "foreign", "exec"];

/// The one type the prelude defines that the compiler builds so far.
const TEXT: &str = "str";

/// The types the language and its library define that the compiler does
/// not build yet.
const TYPES_NOT_BUILT: &[&str] = &[
    "bool", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "usize", "f16", "bf16", "f32",
    "f64", "Nat", "Pos", "Fin", "Ptr", "List", "Map", "Option", "Result",
];

/// The traits the language and its prelude define, which the compiler
/// does not build yet.
const TRAITS_NOT_BUILT: &[&str] = &[
    "Copy",
    "Discard",
    "Add",
    "Sub",
    "Mul",
    "Div",
    "Pow",
    "MatMul",
    "Try",
    "Fmt",
    "Iterate",
    "FromInt",
    "FromFloat",
    "FromString",
    "FromArray",
];

/// What an expression gives.
#[derive(Clone, Debug, PartialEq, Eq)]
enum Ty {
    Text,
    /// A struct, by its place in the file's structs, with a type for each
    /// of its type parameters.
    Struct(usize, Vec<Ty>),
    /// A type parameter of the function, the implementation, the struct,
    /// or the trait the type is written in, by its place among them.
    Param(usize),
    Nothing,
    /// The expression holds a mistake already reported, so what it gives
    /// is not known and nothing about it is reported again. A type that
    /// holds an unknown type is itself unknown.
    Unknown,
}

impl Ty {
    /// The struct `s` with these types, or unknown if one of them is.
    fn structure(s: usize, args: Vec<Ty>) -> Ty {
        if args.contains(&Ty::Unknown) {
            Ty::Unknown
        } else {
            Ty::Struct(s, args)
        }
    }

    /// The type with each type parameter replaced by the type at its place
    /// in `args`.
    fn subst(&self, args: &[Ty]) -> Ty {
        match self {
            Ty::Param(i) => args[*i].clone(),
            Ty::Struct(s, of) => Ty::structure(*s, of.iter().map(|t| t.subst(args)).collect()),
            t => t.clone(),
        }
    }

    /// The same, where a parameter with no type in `args` is unknown.
    fn solved(&self, args: &[Option<Ty>]) -> Ty {
        match self {
            Ty::Param(i) => args[*i].clone().unwrap_or(Ty::Unknown),
            Ty::Struct(s, of) => Ty::structure(*s, of.iter().map(|t| t.solved(args)).collect()),
            t => t.clone(),
        }
    }

    fn mentions(&self, p: usize) -> bool {
        match self {
            Ty::Param(i) => *i == p,
            Ty::Struct(_, args) => args.iter().any(|t| t.mentions(p)),
            _ => false,
        }
    }

    /// How many names it takes to write the type.
    fn size(&self) -> usize {
        match self {
            Ty::Struct(_, args) => 1 + args.iter().map(Ty::size).sum::<usize>(),
            _ => 1,
        }
    }
}

/// Matches `pattern`, whose type parameters stand for the types in `sol`,
/// against `given`, filling in `sol`. Gives whether they fit. A type
/// parameter of `given` stands only for itself.
fn unify(pattern: &Ty, given: &Ty, sol: &mut [Option<Ty>]) -> bool {
    match (pattern, given) {
        (_, Ty::Unknown) | (Ty::Unknown, _) => true,
        (Ty::Param(i), given) => match &sol[*i] {
            None => {
                sol[*i] = Some(given.clone());
                true
            }
            Some(t) => t == given || *t == Ty::Unknown,
        },
        (Ty::Struct(s, a), Ty::Struct(t, b)) => {
            s == t && a.iter().zip(b).all(|(a, b)| unify(a, b, sol))
        }
        (a, b) => a == b,
    }
}

/// Whether some choice of the type parameters in both makes the two types
/// the same. Each side's parameters are in `sol`, those of `b` after those
/// of `a`.
fn overlap(a: &Ty, b: &Ty, sol: &mut Vec<Option<Ty>>) -> bool {
    fn walk(t: &Ty, sol: &[Option<Ty>]) -> Ty {
        let mut t = t.clone();
        while let Ty::Param(i) = t {
            match &sol[i] {
                Some(u) => t = u.clone(),
                None => break,
            }
        }
        t
    }
    fn occurs(p: usize, t: &Ty, sol: &[Option<Ty>]) -> bool {
        match t {
            Ty::Param(i) => *i == p || sol[*i].as_ref().is_some_and(|u| occurs(p, u, sol)),
            Ty::Struct(_, args) => args.iter().any(|t| occurs(p, t, sol)),
            _ => false,
        }
    }
    match (walk(a, sol), walk(b, sol)) {
        (Ty::Param(i), Ty::Param(j)) if i == j => true,
        (Ty::Param(i), t) | (t, Ty::Param(i)) => {
            if occurs(i, &t, sol) {
                false
            } else {
                sol[i] = Some(t);
                true
            }
        }
        (Ty::Struct(s, x), Ty::Struct(t, y)) => {
            s == t && x.iter().zip(&y).all(|(a, b)| overlap(a, b, sol))
        }
        (Ty::Unknown, _) | (_, Ty::Unknown) => true,
        (a, b) => a == b,
    }
}

/// The type with each of its type parameters moved by `by` places.
fn shifted(t: &Ty, by: usize) -> Ty {
    match t {
        Ty::Param(i) => Ty::Param(i + by),
        Ty::Struct(s, args) => Ty::Struct(*s, args.iter().map(|t| shifted(t, by)).collect()),
        t => t.clone(),
    }
}

/// What a type parameter must implement.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Bound {
    None,
    /// A trait, by its place in the file's traits.
    Trait(usize),
    /// A bound already reported as a mistake. Whatever it would allow is
    /// allowed, so nothing it explains is reported.
    Invalid,
}

/// The type parameters a type is written among: their names and bounds.
#[derive(Clone, Debug, Default)]
struct Scope {
    names: Vec<String>,
    bounds: Vec<Bound>,
}

/// What a caller needs to know of a function.
struct Signature {
    /// The type parameters its types are written among.
    scope: Scope,
    params: Vec<(String, Ty)>,
    /// Whether each parameter has a default, so that a call may leave it
    /// out.
    defaults: Vec<bool>,
    gives: Ty,
    /// The effects it carries that a caller must have, which are those it
    /// lists that the compiler builds.
    effects: Vec<String>,
}

impl Signature {
    fn print() -> Signature {
        Signature {
            scope: Scope::default(),
            params: vec![("text".to_string(), Ty::Text)],
            defaults: vec![false],
            gives: Ty::Nothing,
            effects: vec!["console".to_string()],
        }
    }
}

/// An implementation that can be used: its trait, its type parameters,
/// its types, and which of its functions is each of the trait's.
struct ImplInfo {
    of: usize,
    scope: Scope,
    args: Vec<Ty>,
    /// For each function of the trait, the place of the function that
    /// implements it among the implementation's, if one does.
    fns: Vec<Option<usize>>,
    /// Whether it is chosen by calls, which an implementation for types an
    /// earlier one already covers is not.
    chosen: bool,
}

/// The definitions of a file, by name, and what is known of each before
/// any body is checked.
struct Defs<'a> {
    file: &'a File,
    function_names: HashMap<&'a str, usize>,
    struct_names: HashMap<&'a str, usize>,
    trait_names: HashMap<&'a str, usize>,
    /// The functions of traits with each name, as the trait and the
    /// function's place in it.
    trait_fns: HashMap<&'a str, Vec<(usize, usize)>>,
    /// Whether each struct contains itself, directly or through others, as
    /// already reported. A type that names one is not known.
    cyclic: Vec<bool>,
    struct_scopes: Vec<Scope>,
    /// The type of each field of each struct, among its type parameters.
    fields: Vec<Vec<Ty>>,
    signatures: Vec<Signature>,
    /// The signature of each function of each trait, among the trait's
    /// types.
    trait_sigs: Vec<Vec<Signature>>,
    /// Which types of each trait are `out`.
    outs: Vec<Vec<bool>>,
    impls: Vec<Option<ImplInfo>>,
    /// The implementations of each trait, in the order they are written.
    impls_of: Vec<Vec<usize>>,
}

/// What looking for an implementation found.
enum Found {
    /// The types of the trait, `out` ones included, and the
    /// implementation with its types, unless a bound promises it.
    Yes(Vec<Ty>, Option<(usize, Vec<Ty>)>),
    No,
    /// A mistake already reported stands in the way.
    Unknown,
}

impl<'a> Defs<'a> {
    /// The type a type written in the source stands for, among the type
    /// parameters of `scope`. Mistakes go in `errors`.
    fn resolve(&self, t: &Type, scope: &Scope, errors: &mut Vec<Error>) -> Ty {
        let name = t.name.as_str();
        let no_args = |what: &str, errors: &mut Vec<Error>| {
            if !t.args.is_empty() {
                errors.push(Error::new(
                    t.args[0].pos,
                    format!("{what} `{name}` takes no type arguments"),
                ));
            }
        };
        if let Some(p) = scope.names.iter().position(|n| n == name) {
            no_args("the type parameter", errors);
            return if t.args.is_empty() {
                Ty::Param(p)
            } else {
                Ty::Unknown
            };
        }
        if let Some(&s) = self.struct_names.get(name) {
            let args: Vec<Ty> = t
                .args
                .iter()
                .map(|a| self.resolve(a, scope, errors))
                .collect();
            let wanted = self.file.structs[s].generics.len();
            if args.len() != wanted {
                errors.push(Error::new(
                    t.pos,
                    format!(
                        "`{name}` takes {}, and this gives {}",
                        count(wanted, "type argument"),
                        args.len()
                    ),
                ));
                return Ty::Unknown;
            }
            return if self.cyclic[s] {
                Ty::Unknown
            } else {
                Ty::structure(s, args)
            };
        }
        if name == TEXT {
            no_args("the type", errors);
            return if t.args.is_empty() {
                Ty::Text
            } else {
                Ty::Unknown
            };
        }
        errors.push(if self.trait_names.contains_key(name) {
            Error::new(t.pos, format!("`{name}` is a trait, which is not a type"))
        } else if TYPES_NOT_BUILT.contains(&name) {
            Error::unsupported(t.pos, &format!("the type `{name}`"))
        } else {
            Error::new(t.pos, format!("there is no type named `{name}`"))
        });
        Ty::Unknown
    }

    /// The type, with nothing reported.
    fn quietly(&self, t: &Type, scope: &Scope) -> Ty {
        self.resolve(t, scope, &mut Vec::new())
    }

    /// What a bound requires. Mistakes go in `errors`.
    fn bound(&self, bound: &Option<Type>, errors: &mut Vec<Error>) -> Bound {
        let Some(t) = bound else {
            return Bound::None;
        };
        let name = t.name.as_str();
        let error = if !t.args.is_empty() {
            Error::unsupported(t.args[0].pos, "a bound with type arguments")
        } else if let Some(&k) = self.trait_names.get(name) {
            return Bound::Trait(k);
        } else if name == "Nat" {
            Error::unsupported(t.pos, "a size parameter")
        } else if TRAITS_NOT_BUILT.contains(&name) {
            Error::unsupported(t.pos, &format!("the trait `{name}`"))
        } else if self.struct_names.contains_key(name)
            || name == TEXT
            || TYPES_NOT_BUILT.contains(&name)
        {
            Error::new(t.pos, format!("`{name}` is not a trait"))
        } else {
            Error::new(t.pos, format!("there is no trait named `{name}`"))
        };
        errors.push(error);
        Bound::Invalid
    }

    /// The type parameters of a definition, with nothing reported.
    fn scope(&self, generics: &[Generic]) -> Scope {
        Scope {
            names: generics.iter().map(|g| g.name.clone()).collect(),
            bounds: (generics.iter())
                .map(|g| self.bound(&g.bound, &mut Vec::new()))
                .collect(),
        }
    }

    /// A function's signature among the type parameters of `scope`.
    fn signature(&self, f: &Function, scope: Scope) -> Signature {
        Signature {
            params: (f.params.iter())
                .map(|p| (p.name.clone(), self.quietly(&p.ty, &scope)))
                .collect(),
            defaults: f.params.iter().map(|p| p.default.is_some()).collect(),
            gives: match &f.result {
                Some(t) => self.quietly(t, &scope),
                None => Ty::Nothing,
            },
            effects: (f.effects.iter())
                .filter(|(e, _)| e == "console")
                .map(|(e, _)| e.clone())
                .take(1)
                .collect(),
            scope,
        }
    }

    /// How a type is written, with the type parameters of `scope`.
    fn text(&self, ty: &Ty, scope: &Scope) -> String {
        match ty {
            Ty::Text => TEXT.to_string(),
            Ty::Struct(s, args) => {
                let name = &self.file.structs[*s].name;
                if args.is_empty() {
                    name.clone()
                } else {
                    let args: Vec<String> = args.iter().map(|t| self.text(t, scope)).collect();
                    format!("{name}[{}]", args.join(", "))
                }
            }
            Ty::Param(p) => scope.names[*p].clone(),
            Ty::Nothing | Ty::Unknown => "nothing".to_string(),
        }
    }

    /// What a value of the type is, as an error describes it.
    fn describe(&self, ty: &Ty, scope: &Scope) -> String {
        match ty {
            Ty::Text => "text".to_string(),
            Ty::Nothing | Ty::Unknown => "nothing".to_string(),
            ty => format!("a `{}`", self.text(ty, scope)),
        }
    }

    /// What a value of `pattern` is, where the type parameters of `vars`
    /// that have a type in `sol` stand for it.
    fn describe_pattern(
        &self,
        pattern: &Ty,
        sol: &[Option<Ty>],
        vars: &Scope,
        scope: &Scope,
    ) -> String {
        fn text(defs: &Defs, t: &Ty, sol: &[Option<Ty>], vars: &Scope, scope: &Scope) -> String {
            match t {
                Ty::Param(i) => match &sol[*i] {
                    Some(t) => defs.text(t, scope),
                    None => vars.names[*i].clone(),
                },
                Ty::Struct(s, args) if !args.is_empty() => {
                    let args: Vec<String> = args
                        .iter()
                        .map(|a| text(defs, a, sol, vars, scope))
                        .collect();
                    format!("{}[{}]", defs.file.structs[*s].name, args.join(", "))
                }
                t => defs.text(t, scope),
            }
        }
        match pattern.solved(sol) {
            Ty::Text => "text".to_string(),
            _ => format!("a `{}`", text(self, pattern, sol, vars, scope)),
        }
    }

    /// The trait's types other than its `out` ones, as an error names them.
    fn types_of(&self, k: usize, types: &[Ty], scope: &Scope) -> String {
        let shown: Vec<String> = (types.iter().zip(&self.outs[k]))
            .filter(|(_, out)| !**out)
            .map(|(t, _)| self.text(t, scope))
            .collect();
        let shown: Vec<&str> = shown.iter().map(String::as_str).collect();
        names(&shown)
    }

    /// The implementation of trait `k` for `types`, among the type
    /// parameters of `scope`. Only the types that are not `out` are read.
    fn find(
        &self,
        k: usize,
        types: &[Ty],
        scope: &Scope,
        stack: &mut Vec<(usize, Vec<Ty>)>,
    ) -> Found {
        let outs = &self.outs[k];
        let given: Vec<Ty> = (types.iter().zip(outs))
            .filter(|(_, out)| !**out)
            .map(|(t, _)| t.clone())
            .collect();
        let explained = |t: &Ty| {
            *t == Ty::Unknown
                || (0..scope.bounds.len())
                    .any(|p| scope.bounds[p] == Bound::Invalid && t.mentions(p))
        };
        if given.iter().any(explained) {
            return Found::Unknown;
        }
        if let Some(Ty::Param(p)) = given.first()
            && given.iter().all(|t| *t == Ty::Param(*p))
            && scope.bounds[*p] == Bound::Trait(k)
        {
            return Found::Yes(vec![Ty::Param(*p); outs.len()], None);
        }
        let key = (k, given);
        if stack.contains(&key) {
            return Found::No;
        }
        stack.push(key);
        let found = self.find_impl(k, types, scope, stack);
        stack.pop();
        found
    }

    fn find_impl(
        &self,
        k: usize,
        types: &[Ty],
        scope: &Scope,
        stack: &mut Vec<(usize, Vec<Ty>)>,
    ) -> Found {
        for &i in &self.impls_of[k] {
            let Some(info) = &self.impls[i] else {
                continue;
            };
            if !info.chosen {
                continue;
            }
            let mut sol = vec![None; info.scope.names.len()];
            let fits = (info.args.iter().zip(types).zip(&self.outs[k]))
                .filter(|(_, out)| !**out)
                .all(|((pattern, given), _)| unify(pattern, given, &mut sol));
            if !fits {
                continue;
            }
            let Some(sol) = sol.into_iter().collect::<Option<Vec<Ty>>>() else {
                return Found::Unknown;
            };
            for (g, bound) in info.scope.bounds.iter().enumerate() {
                match *bound {
                    Bound::None => {}
                    Bound::Invalid => return Found::Unknown,
                    Bound::Trait(k2) => match self.meets(k2, &sol[g], scope, stack) {
                        Found::Yes(..) => {}
                        other => return other,
                    },
                }
            }
            let all = info.args.iter().map(|t| t.subst(&sol)).collect();
            return Found::Yes(all, Some((i, sol)));
        }
        Found::No
    }

    /// Whether `t` meets the bound `T: Trait`, which asks for an
    /// implementation whose every type is `t`.
    fn meets(&self, k: usize, t: &Ty, scope: &Scope, stack: &mut Vec<(usize, Vec<Ty>)>) -> Found {
        let types = vec![t.clone(); self.outs[k].len()];
        match self.find(k, &types, scope, stack) {
            Found::Yes(all, by) if all.iter().all(|u| u == t) => Found::Yes(all, by),
            Found::Yes(..) => Found::No,
            other => other,
        }
    }
}

/// A count of things, as a sentence gives it.
fn count(n: usize, thing: &str) -> String {
    if n == 1 {
        format!("1 {thing}")
    } else {
        format!("{n} {thing}s")
    }
}

/// A list of names, each in backticks, joined as a sentence joins them.
fn names(list: &[&str]) -> String {
    let quoted: Vec<String> = list.iter().map(|n| format!("`{n}`")).collect();
    match quoted.as_slice() {
        [] => String::new(),
        [one] => one.clone(),
        [a, b] => format!("{a} and {b}"),
        [rest @ .., last] => format!("{}, and {last}", rest.join(", ")),
    }
}

/// How a type is written in the source.
fn written_type(t: &Type) -> String {
    if t.args.is_empty() {
        t.name.clone()
    } else {
        let args: Vec<String> = t.args.iter().map(written_type).collect();
        format!("{}[{}]", t.name, args.join(", "))
    }
}

/// A function's signature as it is written, with `effects` in place of
/// its list of effects.
fn written(f: &Function, effects: &[&str]) -> String {
    let generics: Vec<String> = (f.generics.iter())
        .map(|g| match &g.bound {
            Some(b) => format!("{}: {}", g.name, written_type(b)),
            None => g.name.clone(),
        })
        .collect();
    let params: Vec<String> = (f.params.iter())
        .map(|p| {
            let mut text = format!("{}: {}", p.name, written_type(&p.ty));
            if let Some(value) = &p.default {
                text.push_str(&format!(" = {}", written_expr(value)));
            }
            text
        })
        .collect();
    let mut text = format!("fn {}", f.name);
    if !generics.is_empty() {
        text.push_str(&format!("[{}]", generics.join(", ")));
    }
    text.push_str(&format!("({})", params.join(", ")));
    if let Some(t) = &f.result {
        text.push_str(&format!(" -> {}", written_type(t)));
    }
    if !effects.is_empty() {
        text.push_str(&format!(" ! {}", effects.join(", ")));
    }
    text
}

/// How an expression is written in the source, with each text written with
/// an escape for every character that needs one.
fn written_expr(e: &Expr) -> String {
    match e {
        Expr::Text { text, .. } => {
            let mut out = String::from("\"");
            for c in String::from_utf8_lossy(text).chars() {
                match c {
                    '"' | '\\' | '{' | '}' => {
                        out.push('\\');
                        out.push(c);
                    }
                    '\n' => out.push_str("\\n"),
                    '\t' => out.push_str("\\t"),
                    c => out.push(c),
                }
            }
            out.push('"');
            out
        }
        Expr::Name { name, .. } => name.clone(),
        Expr::Call(call) => {
            let args: Vec<String> = (call.args.iter())
                .map(|a| match &a.name {
                    Some((name, _)) => format!("{name}: {}", written_expr(&a.value)),
                    None => written_expr(&a.value),
                })
                .collect();
            match &call.qualifier {
                Some((q, _)) => format!("{q}.{}({})", call.callee, args.join(", ")),
                None => format!("{}({})", call.callee, args.join(", ")),
            }
        }
        Expr::Field { of, name, .. } => format!("{}.{name}", written_expr(of)),
    }
}

/// A definition of the file, by its place among those of its kind.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Item {
    Fn(usize),
    Struct(usize),
    Trait(usize),
    Impl(usize),
}

/// The kinds of definition that claim a name.
#[derive(Clone, Copy, PartialEq, Eq)]
enum Claim {
    Fn(usize),
    Struct(usize),
    Trait(usize),
    /// A function of a trait: the trait, and its place there.
    TraitFn(usize, usize),
}

/// A file that has no error: the program it describes, and the warnings
/// `smith check` reports about it.
pub struct Checked {
    pub program: ir::Program,
    pub warnings: Vec<Error>,
}

/// Checks the file, and lowers what `main` reaches. Every error is
/// reported, except one that a mistake already reported would explain.
pub fn check(file: File) -> Result<Checked, Vec<Error>> {
    let mut errors = Vec::new();

    // The first definition of a name holds it, and a later one is a
    // mistake. Types and functions have names of their own, and the
    // functions of different traits may share one.
    let mut defined: Vec<(Pos, &str, Claim)> = Vec::new();
    defined.extend(
        (file.functions.iter().enumerate()).map(|(i, f)| (f.pos, f.name.as_str(), Claim::Fn(i))),
    );
    defined.extend(
        (file.structs.iter().enumerate()).map(|(i, s)| (s.pos, s.name.as_str(), Claim::Struct(i))),
    );
    for (k, t) in file.traits.iter().enumerate() {
        defined.push((t.pos, t.name.as_str(), Claim::Trait(k)));
        for (i, f) in t.functions.iter().enumerate() {
            defined.push((f.pos, f.name.as_str(), Claim::TraitFn(k, i)));
        }
    }
    defined.sort_by_key(|(pos, _, _)| *pos);
    let mut function_names: HashMap<&str, usize> = HashMap::new();
    let mut struct_names: HashMap<&str, usize> = HashMap::new();
    let mut trait_names: HashMap<&str, usize> = HashMap::new();
    let mut trait_fns: HashMap<&str, Vec<(usize, usize)>> = HashMap::new();
    for (pos, name, claim) in defined {
        let is_type = matches!(claim, Claim::Struct(_) | Claim::Trait(_));
        let prelude = |what: &str| {
            Error::unsupported(
                pos,
                &format!("defining `{what}`, which the prelude defines,"),
            )
        };
        let of_prelude = match claim {
            Claim::Fn(_) | Claim::TraitFn(..) => name == "print",
            Claim::Struct(_) => name == TEXT,
            Claim::Trait(_) => name == TEXT || TRAITS_NOT_BUILT.contains(&name),
        };
        if of_prelude {
            errors.push(prelude(name));
        } else if is_type && struct_names.contains_key(name) {
            errors.push(Error::new(
                pos,
                format!("there is already a struct named `{name}`"),
            ));
        } else if is_type && trait_names.contains_key(name) {
            errors.push(Error::new(
                pos,
                format!("there is already a trait named `{name}`"),
            ));
        } else if !is_type && function_names.contains_key(name) {
            errors.push(Error::new(
                pos,
                format!("there is already a function named `{name}`"),
            ));
        } else {
            match claim {
                Claim::Fn(i) => {
                    if let Some(&(k, _)) = trait_fns.get(name).and_then(|f| f.first()) {
                        errors.push(Error::new(
                            pos,
                            format!(
                                "there is already a function named `{name}`, in the trait `{}`",
                                file.traits[k].name
                            ),
                        ));
                    } else {
                        function_names.insert(name, i);
                    }
                }
                Claim::Struct(i) => {
                    struct_names.insert(name, i);
                }
                Claim::Trait(k) => {
                    trait_names.insert(name, k);
                }
                Claim::TraitFn(k, i) => {
                    let list = trait_fns.entry(name).or_default();
                    // A second function of one trait with the name is
                    // reported with the trait.
                    if !list.iter().any(|(t, _)| *t == k) {
                        list.push((k, i));
                    }
                }
            }
        }
    }
    let Some(&main) = function_names.get("main") else {
        errors.push(Error::new(
            Pos::START,
            "there is no function named `main`, where the program starts",
        ));
        return Err(errors);
    };

    // What `main` reaches, each in the order it is first reached.
    let mut reached: Vec<Item> = vec![Item::Fn(main)];
    let mut i = 0;
    while i < reached.len() {
        let mut found: Vec<Item> = Vec::new();
        let type_name = |name: &str, found: &mut Vec<Item>| {
            if let Some(&s) = struct_names.get(name) {
                found.push(Item::Struct(s));
            } else if let Some(&k) = trait_names.get(name) {
                found.push(Item::Trait(k));
            }
        };
        let called = |calls: Vec<&Call>, found: &mut Vec<Item>| {
            for call in calls {
                if let Some((q, _)) = &call.qualifier {
                    type_name(q, found);
                    continue;
                }
                let name = call.callee.as_str();
                if let Some(&f) = function_names.get(name) {
                    found.push(Item::Fn(f));
                }
                type_name(name, found);
                for &(k, _) in trait_fns.get(name).into_iter().flatten() {
                    found.push(Item::Trait(k));
                }
            }
        };
        let function = |f: &Function, found: &mut Vec<Item>| {
            for name in types_named(f) {
                type_name(name, found);
            }
            let defaults = f.params.iter().filter_map(|p| p.default.as_ref());
            called(calls(values(&f.body).chain(defaults)), found);
        };
        match reached[i] {
            Item::Fn(f) => function(&file.functions[f], &mut found),
            Item::Struct(s) => {
                let s = &file.structs[s];
                for field in &s.fields {
                    for (name, _) in type_names(&field.ty) {
                        type_name(name, &mut found);
                    }
                }
                for name in bounds_named(&s.generics) {
                    type_name(name, &mut found);
                }
                let defaults = s.fields.iter().filter_map(|f| f.default.as_ref());
                called(calls(defaults), &mut found);
                // An implementation lives with its trait or with a type it
                // is for.
                for (i, im) in file.impls.iter().enumerate() {
                    let names: Vec<&str> = (im.args.iter())
                        .flat_map(type_names)
                        .map(|(name, _)| name)
                        .collect();
                    if names.contains(&s.name.as_str()) {
                        found.push(Item::Impl(i));
                    }
                }
            }
            Item::Trait(k) => {
                let t = &file.traits[k];
                for f in &t.functions {
                    function(f, &mut found);
                }
                for name in bounds_named(&t.generics) {
                    type_name(name, &mut found);
                }
                for (i, im) in file.impls.iter().enumerate() {
                    if im.name == t.name {
                        found.push(Item::Impl(i));
                    }
                }
            }
            Item::Impl(i) => {
                let im = &file.impls[i];
                type_name(&im.name, &mut found);
                for t in &im.args {
                    for (name, _) in type_names(t) {
                        type_name(name, &mut found);
                    }
                }
                for name in bounds_named(&im.generics) {
                    type_name(name, &mut found);
                }
                for f in &im.functions {
                    function(f, &mut found);
                }
            }
        }
        for item in found {
            if !reached.contains(&item) {
                reached.push(item);
            }
        }
        i += 1;
    }
    let reached_structs: Vec<usize> = (reached.iter())
        .filter_map(|r| match r {
            Item::Struct(s) => Some(*s),
            _ => None,
        })
        .collect();

    let mut defs = Defs {
        file: &file,
        function_names,
        struct_names,
        trait_names,
        trait_fns,
        cyclic: vec![false; file.structs.len()],
        struct_scopes: Vec::new(),
        fields: Vec::new(),
        signatures: Vec::new(),
        trait_sigs: Vec::new(),
        outs: (file.traits.iter())
            .map(|t| t.generics.iter().map(|g| g.out.is_some()).collect())
            .collect(),
        impls: Vec::new(),
        impls_of: vec![Vec::new(); file.traits.len()],
    };
    let mut done = Vec::new();
    for &s in &reached_structs {
        if !done.contains(&s) {
            cycles(&mut defs, s, &mut Vec::new(), &mut done, &mut errors);
        }
    }
    defs.struct_scopes = file
        .structs
        .iter()
        .map(|s| defs.scope(&s.generics))
        .collect();
    defs.fields = (file.structs.iter().enumerate())
        .map(|(i, s)| {
            (s.fields.iter())
                .map(|f| defs.quietly(&f.ty, &defs.struct_scopes[i]))
                .collect()
        })
        .collect();
    defs.signatures = (file.functions.iter())
        .map(|f| defs.signature(f, defs.scope(&f.generics)))
        .collect();
    defs.trait_sigs = (file.traits.iter())
        .map(|t| {
            let scope = Scope {
                names: t.generics.iter().map(|g| g.name.clone()).collect(),
                bounds: vec![Bound::None; t.generics.len()],
            };
            (t.functions.iter())
                .map(|f| defs.signature(f, scope.clone()))
                .collect()
        })
        .collect();
    for (i, im) in file.impls.iter().enumerate() {
        let info = impl_info(&defs, im);
        if let Some(info) = &info {
            defs.impls_of[info.of].push(i);
        }
        defs.impls.push(info);
    }
    // An implementation for types an earlier one covers is not chosen.
    for k in 0..file.traits.len() {
        for (at, &j) in defs.impls_of[k].iter().enumerate() {
            let covered = defs.impls_of[k][..at].iter().any(|&i| {
                let (Some(a), Some(b)) = (&defs.impls[i], &defs.impls[j]) else {
                    return false;
                };
                a.chosen && overlaps(&defs.outs[k], a, b)
            });
            if covered && let Some(info) = &mut defs.impls[j] {
                info.chosen = false;
            }
        }
    }

    let mut checker = Checker {
        defs: &defs,
        errors,
    };
    checker.main(&file.functions[main]);
    for &item in &reached {
        match item {
            Item::Fn(f) => checker.signature(f),
            Item::Struct(s) => checker.structure(s),
            Item::Trait(k) => checker.declared_trait(k),
            Item::Impl(i) => checker.implementation(i),
        }
    }
    for &item in &reached {
        match item {
            Item::Fn(f) => checker.body(Template::Free(f)),
            Item::Impl(i) => {
                if let Some(info) = &defs.impls[i] {
                    for (fi, at) in info.fns.iter().enumerate() {
                        if at.is_some() {
                            checker.body(Template::Impl(i, fi));
                        }
                    }
                }
            }
            Item::Struct(_) | Item::Trait(_) => {}
        }
    }
    checker.recursion(main, &mut Vec::new(), &mut Vec::new());
    if !checker.errors.is_empty() {
        return Err(checker.errors);
    }
    let start = Instance {
        template: Template::Free(main),
        args: Vec::new(),
    };
    checker.copies(&start, &mut Vec::new(), &mut Vec::new());
    if !checker.errors.is_empty() {
        return Err(checker.errors);
    }

    let mut lowering = Lowering {
        defs: &defs,
        instances: vec![start],
        texts: Vec::new(),
    };
    let mut lowered = Vec::new();
    while lowered.len() < lowering.instances.len() {
        let instance = lowering.instances[lowered.len()].clone();
        lowered.push(lowering.function(&instance));
    }
    let program = ir::Program {
        texts: lowering.texts,
        functions: lowered,
        main: 0,
    };
    Ok(Checked {
        program,
        warnings: warnings(&file, &reached),
    })
}

/// A warning for each definition `main` does not reach, at its name, and
/// for each binding whose name is never read, at the name.
fn warnings(file: &File, reached: &[Item]) -> Vec<Error> {
    let mut out = Vec::new();
    let mut dead = |item: Item, pos: Pos, what: String| {
        if !reached.contains(&item) {
            out.push(
                Error::new(
                    pos,
                    format!("{what} is dead code: `main` does not reach it"),
                )
                .with_fix("delete it, or use it from code that `main` reaches"),
            );
        }
    };
    for (i, f) in file.functions.iter().enumerate() {
        dead(Item::Fn(i), f.pos, format!("the function `{}`", f.name));
    }
    for (i, s) in file.structs.iter().enumerate() {
        dead(Item::Struct(i), s.pos, format!("the struct `{}`", s.name));
    }
    for (i, t) in file.traits.iter().enumerate() {
        dead(Item::Trait(i), t.pos, format!("the trait `{}`", t.name));
    }
    for (i, im) in file.impls.iter().enumerate() {
        let args: Vec<String> = im.args.iter().map(written_type).collect();
        let what = format!("the implementation `{}[{}]`", im.name, args.join(", "));
        dead(Item::Impl(i), im.pos, what);
    }
    let bodies = (file.functions.iter()).chain(file.impls.iter().flat_map(|im| &im.functions));
    for f in bodies {
        unused_bindings(f, &mut out);
    }
    out
}

/// A warning for each binding of the function whose name its body never
/// reads. A name is defined once in a function, so any read of it is a
/// read of that binding.
fn unused_bindings(f: &Function, out: &mut Vec<Error>) {
    let read: Vec<&str> = (exprs(values(&f.body)).into_iter())
        .filter_map(|e| match e {
            Expr::Name { name, .. } => Some(name.as_str()),
            _ => None,
        })
        .collect();
    for stmt in &f.body {
        if let Stmt::Define { name, pos, .. } = stmt
            && !read.contains(&name.as_str())
        {
            out.push(
                Error::new(
                    *pos,
                    format!("`{name}` is defined in `{}` and never read", f.name),
                )
                .with_fix(format!("delete the binding, or read `{name}`")),
            );
        }
    }
}

/// What is known of an implementation before it is checked, if its trait
/// and its types are known.
fn impl_info(defs: &Defs, im: &crate::parser::Impl) -> Option<ImplInfo> {
    let &of = defs.trait_names.get(im.name.as_str())?;
    let t = &defs.file.traits[of];
    if im.args.len() != t.generics.len() {
        return None;
    }
    let scope = defs.scope(&im.generics);
    let args: Vec<Ty> = im.args.iter().map(|a| defs.quietly(a, &scope)).collect();
    if args.contains(&Ty::Unknown) {
        return None;
    }
    // Each type parameter must be chosen by the types that are not `out`.
    let chosen = (0..scope.names.len())
        .all(|p| (args.iter().zip(&defs.outs[of])).any(|(a, out)| !out && a.mentions(p)));
    let fns = (t.functions.iter())
        .map(|f| im.functions.iter().position(|g| g.name == f.name))
        .collect();
    Some(ImplInfo {
        of,
        scope,
        args,
        fns,
        chosen,
    })
}

/// Whether two implementations of a trait are for some of the same types.
fn overlaps(outs: &[bool], a: &ImplInfo, b: &ImplInfo) -> bool {
    let by = a.scope.names.len();
    let mut sol = vec![None; by + b.scope.names.len()];
    (a.args.iter().zip(&b.args).zip(outs))
        .filter(|(_, out)| !**out)
        .all(|((x, y), _)| overlap(x, &shifted(y, by), &mut sol))
}

/// Reports each field whose type closes a cycle of structs that contain one
/// another, in a walk of the fields from struct `s`, and marks the structs
/// on the cycle. `path` is the structs being walked, and `done` those
/// already walked.
fn cycles(
    defs: &mut Defs,
    s: usize,
    path: &mut Vec<usize>,
    done: &mut Vec<usize>,
    errors: &mut Vec<Error>,
) {
    path.push(s);
    for field in &defs.file.structs[s].fields {
        for (name, pos) in type_names(&field.ty) {
            let Some(&t) = defs.struct_names.get(name) else {
                continue;
            };
            if let Some(at) = path.iter().position(|p| *p == t) {
                errors.push(Error::unsupported(
                    pos,
                    "a struct that contains itself, directly or through others,",
                ));
                for &p in &path[at..] {
                    defs.cyclic[p] = true;
                }
            } else if !done.contains(&t) {
                cycles(defs, t, path, done, errors);
            }
        }
    }
    path.pop();
    done.push(s);
}

/// Every name in a type, with where it is: its own, and those of the
/// types in its brackets.
fn type_names(t: &Type) -> Vec<(&str, Pos)> {
    let mut out = vec![(t.name.as_str(), t.pos)];
    for a in &t.args {
        out.extend(type_names(a));
    }
    out
}

/// The names the bounds of type parameters use.
fn bounds_named(generics: &[Generic]) -> Vec<&str> {
    (generics.iter())
        .filter_map(|g| g.bound.as_ref())
        .flat_map(type_names)
        .map(|(name, _)| name)
        .collect()
}

/// The error for assigning to `name`, or to a field of it, where `name`,
/// the name at `at` among those of the body, is not a `var`.
fn not_a_var(body: &Body, name: &str, pos: Pos, at: usize) -> Error {
    let error = Error::new(
        pos,
        format!("`{name}` is not a `var`, so it cannot be assigned to"),
    );
    if at < body.params {
        error.with_fix(format!(
            "declare a `var` that starts as the parameter, as in `var new_{name} = {name}`"
        ))
    } else {
        error.with_fix(format!(
            "declare it with `var`: write `var {name}` where it is defined"
        ))
    }
}

/// The value of each statement of a body, in order.
fn values(body: &[Stmt]) -> impl Iterator<Item = &Expr> {
    body.iter().map(|stmt| match stmt {
        Stmt::Define { value, .. } | Stmt::Assign { value, .. } | Stmt::Expr(value) => value,
    })
}

fn walk<'a>(expr: &'a Expr, out: &mut Vec<&'a Expr>) {
    out.push(expr);
    match expr {
        Expr::Call(call) => {
            for arg in &call.args {
                walk(&arg.value, out);
            }
        }
        Expr::Field { of, .. } => walk(of, out),
        Expr::Text { .. } | Expr::Name { .. } => {}
    }
}

/// Every expression in the expressions, in order, each before the
/// expressions inside it, and then from left to right.
fn exprs<'a>(exprs: impl IntoIterator<Item = &'a Expr>) -> Vec<&'a Expr> {
    let mut inside = Vec::new();
    for e in exprs {
        walk(e, &mut inside);
    }
    inside
}

/// Every call in the expressions, in order, each outermost first and then
/// from left to right. A construction is written as a call, so it is among
/// them.
fn calls<'a>(values: impl IntoIterator<Item = &'a Expr>) -> Vec<&'a Call> {
    (exprs(values).into_iter())
        .filter_map(|e| match e {
            Expr::Call(call) => Some(call),
            _ => None,
        })
        .collect()
}

/// The error for a call in a default that leaves out that default, directly
/// or through the defaults it leaves out in turn.
fn default_cycle(pos: Pos) -> Error {
    Error::new(
        pos,
        "this call leaves out the default it is written in, directly or through others, \
         so filling it in would never end",
    )
}

/// Every name a function uses as a type: in its signature, in its
/// bindings, and in what its body calls, which may be a struct.
fn types_named(f: &Function) -> Vec<&str> {
    let mut types: Vec<&Type> = f.params.iter().map(|p| &p.ty).collect();
    types.extend(&f.result);
    for stmt in &f.body {
        if let Stmt::Define { ty: Some(t), .. } = stmt {
            types.push(t);
        }
    }
    let mut out: Vec<&str> = (types.into_iter())
        .flat_map(type_names)
        .map(|(name, _)| name)
        .collect();
    out.extend(bounds_named(&f.generics));
    out
}

/// A function with a body: one written on its own, or the function of an
/// implementation that implements the trait's function at a place.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Template {
    Free(usize),
    Impl(usize, usize),
}

/// A copy of a function, for a choice of its type parameters.
#[derive(Clone, Debug, PartialEq, Eq)]
struct Instance {
    template: Template,
    args: Vec<Ty>,
}

/// A function with a body, as its body sees it: its signature is the
/// trait's, for the function of an implementation.
struct View<'a> {
    f: &'a Function,
    scope: &'a Scope,
    params: Vec<(String, Ty)>,
    gives: Ty,
    /// How its result type is written, if it has one.
    result: Option<String>,
    /// The effects it lists.
    effects: &'a [(String, Pos)],
}

/// What a checked call does, once every type in it is known.
enum Callee {
    Print,
    Construct(usize, Vec<Ty>),
    /// A copy of a function to run, and what it gives.
    Run(Instance, Ty),
}

/// For each argument of a checked call, the place of its parameter.
fn params_of(params: &[(String, Ty)], args: &[Arg]) -> Vec<usize> {
    let mut next = 0;
    (args.iter())
        .map(|arg| match &arg.name {
            Some((name, _)) => params.iter().position(|(p, _)| p == name).expect("checked"),
            None => {
                next += 1;
                next - 1
            }
        })
        .collect()
}

impl<'a> Defs<'a> {
    /// The defaults a call leaves out, each with the place of its parameter
    /// or field, in the order of the parameters or fields. A call that
    /// names nothing there is leaves out nothing.
    fn left_out(&self, call: &Call) -> Vec<(usize, &'a Expr)> {
        let file = self.file;
        let params = |f: &'a Function| -> Vec<(&'a str, Option<&'a Expr>)> {
            (f.params.iter())
                .map(|p| (p.name.as_str(), p.default.as_ref()))
                .collect()
        };
        let name = call.callee.as_str();
        let (slots, by_position) = match &call.qualifier {
            Some((q, _)) => {
                let Some(&k) = self.trait_names.get(q.as_str()) else {
                    return Vec::new();
                };
                match file.traits[k].functions.iter().find(|f| f.name == name) {
                    Some(f) => (params(f), true),
                    None => return Vec::new(),
                }
            }
            None => {
                if let Some(&s) = self.struct_names.get(name) {
                    let fields = (file.structs[s].fields.iter())
                        .map(|f| (f.name.as_str(), f.default.as_ref()))
                        .collect();
                    (fields, false)
                } else if name == "print" {
                    return Vec::new();
                } else if let Some(&f) = self.function_names.get(name) {
                    (params(&file.functions[f]), true)
                } else if let Some([(k, fi)]) = self.trait_fns.get(name).map(Vec::as_slice) {
                    (params(&file.traits[*k].functions[*fi]), true)
                } else {
                    return Vec::new();
                }
            }
        };
        let mut given = vec![false; slots.len()];
        let mut next = 0;
        for arg in &call.args {
            match &arg.name {
                Some((n, _)) => {
                    if let Some(i) = slots.iter().position(|(s, _)| s == n) {
                        given[i] = true;
                    }
                }
                None if by_position => {
                    if let Some(g) = given.get_mut(next) {
                        *g = true;
                    }
                    next += 1;
                }
                None => {}
            }
        }
        (slots.into_iter().zip(given).enumerate())
            .filter_map(|(i, ((_, default), given))| default.filter(|_| !given).map(|d| (i, d)))
            .collect()
    }

    /// The calls in the defaults a call leaves out, which run where the
    /// call is written, each followed by the calls in the defaults it
    /// leaves out in turn. A default already in `stack` is not filled in
    /// again, and the place of the call that leaves it out goes in
    /// `cycles`.
    fn defaults_called(
        &self,
        call: &Call,
        stack: &mut Vec<&'a Expr>,
        out: &mut Vec<&'a Call>,
        cycles: &mut Vec<Pos>,
    ) {
        for (_, default) in self.left_out(call) {
            if stack.iter().any(|d| std::ptr::eq(*d, default)) {
                cycles.push(call.pos);
                continue;
            }
            stack.push(default);
            for inner in calls([default]) {
                out.push(inner);
                self.defaults_called(inner, stack, out, cycles);
            }
            stack.pop();
        }
    }

    fn view(&self, t: Template) -> View<'_> {
        let file = self.file;
        match t {
            Template::Free(f) => {
                let sig = &self.signatures[f];
                let def = &file.functions[f];
                View {
                    f: def,
                    scope: &sig.scope,
                    params: sig.params.clone(),
                    gives: sig.gives.clone(),
                    result: def.result.as_ref().map(written_type),
                    effects: &def.effects,
                }
            }
            Template::Impl(i, fi) => {
                let info = self.impls[i]
                    .as_ref()
                    .expect("an implementation that is known");
                let sig = &self.trait_sigs[info.of][fi];
                let gives = sig.gives.subst(&info.args);
                View {
                    f: &file.impls[i].functions[info.fns[fi].expect("implemented")],
                    scope: &info.scope,
                    params: (sig.params.iter())
                        .map(|(n, t)| (n.clone(), t.subst(&info.args)))
                        .collect(),
                    result: (gives != Ty::Nothing).then(|| self.text(&gives, &info.scope)),
                    gives,
                    effects: &file.traits[info.of].functions[fi].effects,
                }
            }
        }
    }

    /// The type of a checked expression, where every name has a type with
    /// no type parameters.
    fn type_of(&self, e: &Expr, names: &[(String, Ty)]) -> Ty {
        match e {
            Expr::Text { .. } => Ty::Text,
            Expr::Name { name, .. } => {
                let found = names.iter().rev().find(|(n, _)| n == name);
                found.expect("checked").1.clone()
            }
            Expr::Call(call) => match self.callee(call, names) {
                Callee::Print => Ty::Nothing,
                Callee::Construct(s, args) => Ty::Struct(s, args),
                Callee::Run(_, gives) => gives,
            },
            Expr::Field { of, name, .. } => {
                let Ty::Struct(s, args) = self.type_of(of, names) else {
                    unreachable!("checked")
                };
                let at = self.file.structs[s]
                    .fields
                    .iter()
                    .position(|f| &f.name == name);
                self.fields[s][at.expect("checked")].subst(&args)
            }
        }
    }

    /// What a checked call does, where every name has a type with no type
    /// parameters.
    fn callee(&self, call: &Call, names: &[(String, Ty)]) -> Callee {
        let given = |i: usize| self.type_of(&call.args[i].value, names);
        let solve = |params: &[(String, Ty)], vars: usize| {
            let mut sol = vec![None; vars];
            for (i, p) in params_of(params, &call.args).into_iter().enumerate() {
                unify(&params[p].1, &given(i), &mut sol);
            }
            sol.into_iter()
                .map(|t| t.unwrap_or(Ty::Unknown))
                .collect::<Vec<Ty>>()
        };
        let (k, fi) = match &call.qualifier {
            None => {
                let name = call.callee.as_str();
                if let Some(&s) = self.struct_names.get(name) {
                    let def = &self.file.structs[s];
                    let mut sol = vec![None; def.generics.len()];
                    for (i, arg) in call.args.iter().enumerate() {
                        let (field, _) = arg.name.as_ref().expect("checked");
                        let at = def.fields.iter().position(|f| &f.name == field);
                        unify(&self.fields[s][at.expect("checked")], &given(i), &mut sol);
                    }
                    let args = sol.into_iter().map(|t| t.expect("checked")).collect();
                    return Callee::Construct(s, args);
                }
                if name == "print" {
                    return Callee::Print;
                }
                if let Some(&f) = self.function_names.get(name) {
                    let sig = &self.signatures[f];
                    let args = solve(&sig.params, sig.scope.names.len());
                    let gives = sig.gives.subst(&args);
                    let template = Template::Free(f);
                    return Callee::Run(Instance { template, args }, gives);
                }
                self.trait_fns[name][0]
            }
            Some((q, _)) => {
                let k = self.trait_names[q.as_str()];
                let at = self.file.traits[k]
                    .functions
                    .iter()
                    .position(|f| f.name == call.callee);
                (k, at.expect("checked"))
            }
        };
        let sig = &self.trait_sigs[k][fi];
        let types = solve(&sig.params, sig.scope.names.len());
        let Found::Yes(all, Some((i, args))) =
            self.find(k, &types, &Scope::default(), &mut Vec::new())
        else {
            unreachable!("checked")
        };
        let template = Template::Impl(i, fi);
        Callee::Run(Instance { template, args }, sig.gives.subst(&all))
    }

    /// The calls in a copy's body that run a function, outermost first and
    /// then from left to right, each with where it is and the copy it runs.
    /// The calls in a default a call leaves out follow that call. Each call
    /// that leaves out the default it is written in is in the second list.
    fn calls_of(&self, instance: &Instance) -> (Vec<(Pos, Instance)>, Vec<Pos>) {
        let view = self.view(instance.template);
        let mut names: Vec<(String, Ty)> = (view.params.iter())
            .map(|(n, t)| (n.clone(), t.subst(&instance.args)))
            .collect();
        let mut out = Vec::new();
        let mut cycles = Vec::new();
        for (stmt, value) in view.f.body.iter().zip(values(&view.f.body)) {
            for call in calls([value]) {
                if let Callee::Run(callee, _) = self.callee(call, &names) {
                    out.push((call.pos, callee));
                }
                // A default uses no name, so its calls are the same
                // wherever it is filled in.
                let mut filled = Vec::new();
                self.defaults_called(call, &mut Vec::new(), &mut filled, &mut cycles);
                for inner in filled {
                    if let Callee::Run(callee, _) = self.callee(inner, &[]) {
                        out.push((inner.pos, callee));
                    }
                }
            }
            if let Stmt::Define { name, value, .. } = stmt
                && !names.iter().any(|(n, _)| n == name)
            {
                let ty = self.type_of(value, &names);
                names.push((name.clone(), ty));
            }
        }
        (out, cycles)
    }
}

/// What kind of definition a list of type parameters belongs to.
#[derive(Clone, Copy, PartialEq, Eq)]
enum Owner {
    Function,
    Struct,
    Trait,
    Impl,
}

struct Checker<'a> {
    defs: &'a Defs<'a>,
    errors: Vec<Error>,
}

/// What checking one body needs to keep.
struct Body<'a> {
    /// The function whose body this is, or `None` for a default.
    f: Option<&'a Function>,
    /// For a default, the names of the parameters of the function it is
    /// in, which it may not use.
    default: Option<Vec<&'a str>>,
    scope: &'a Scope,
    /// The effects it lists that have a name, each once.
    listed: Vec<&'a str>,
    /// Whether it lists an effect with no such name, which may be one it
    /// needs misspelt, so that a missing effect is then not reported.
    unknown_effect: bool,
    /// The effects already reported missing, each reported once.
    missing: Vec<String>,
    /// The names defined so far, and what each gives.
    names: Vec<(String, Ty)>,
    /// How many of the names are parameters, which come first.
    params: usize,
    /// The names defined with `var`.
    vars: Vec<String>,
}

impl<'a> Body<'a> {
    /// The function whose body this is.
    fn owner(&self) -> &'a Function {
        self.f.expect("a function's body")
    }
}

impl<'a> Checker<'a> {
    /// What only `main` must be.
    fn main(&mut self, f: &Function) {
        if let Some(g) = f.generics.first() {
            self.errors
                .push(Error::unsupported(g.pos, "a type parameter of `main`"));
        }
        if let Some(p) = f.params.first() {
            self.errors
                .push(Error::unsupported(p.pos, "a parameter of `main`"));
        }
        if let Some(t) = &f.result {
            self.errors
                .push(Error::unsupported(t.pos, "a return type on `main`"));
        }
    }

    /// Checks that a type written in the source is one there is, among the
    /// type parameters of `scope`.
    fn ty(&mut self, t: &Type, scope: &Scope) {
        self.defs.resolve(t, scope, &mut self.errors);
    }

    /// Checks a list of type parameters: each named once, with a name no
    /// type has, `out` only in a trait, and each bound a trait.
    fn generics(&mut self, generics: &[Generic], owner: &str, kind: Owner) {
        let defs = self.defs;
        for (i, g) in generics.iter().enumerate() {
            let name = g.name.as_str();
            if generics[..i].iter().any(|h| h.name == g.name) {
                self.errors.push(Error::new(
                    g.pos,
                    format!("`{name}` is already a type parameter of {owner}"),
                ));
            } else if name == TEXT
                || defs.struct_names.contains_key(name)
                || defs.trait_names.contains_key(name)
            {
                self.errors.push(Error::new(
                    g.pos,
                    format!("`{name}` is already the name of a type"),
                ));
            }
            if let Some(at) = g.out
                && kind != Owner::Trait
            {
                self.errors
                    .push(Error::new(at, "only the types of a trait can be `out`"));
            }
            match (&g.bound, kind) {
                (Some(b), Owner::Struct) => self.errors.push(Error::unsupported(
                    b.pos,
                    "a bound on a type parameter of a struct",
                )),
                (Some(b), Owner::Trait) => self
                    .errors
                    .push(Error::unsupported(b.pos, "a bound on a type of a trait")),
                (bound, _) => {
                    defs.bound(bound, &mut self.errors);
                }
            }
        }
    }

    /// Checks a struct: its type parameters, each named by a field, and its
    /// fields, each named once, each of a type there is.
    fn structure(&mut self, s: usize) {
        let defs = self.defs;
        let def = &defs.file.structs[s];
        let scope = &defs.struct_scopes[s];
        self.generics(&def.generics, &format!("`{}`", def.name), Owner::Struct);
        for g in &def.generics {
            let named =
                (def.fields.iter()).any(|f| type_names(&f.ty).iter().any(|(n, _)| *n == g.name));
            if !named {
                self.errors.push(Error::unsupported(
                    g.pos,
                    "a type parameter that no field's type names",
                ));
            }
        }
        for (i, field) in def.fields.iter().enumerate() {
            if def.fields[..i].iter().any(|f| f.name == field.name) {
                self.errors.push(Error::new(
                    field.pos,
                    format!("`{}` is already a field of `{}`", field.name, def.name),
                ));
            }
            self.ty(&field.ty, scope);
        }
        for (field, ty) in def.fields.iter().zip(&defs.fields[s]) {
            if let Some(value) = &field.default {
                self.default(value, ty, scope, Vec::new());
            }
        }
    }

    /// Checks a default: it uses no name, carries no effect, and gives
    /// `needed`, among the type parameters of `scope`. `params` are the
    /// parameters of the function it is in, if it is in one.
    fn default(&mut self, value: &Expr, needed: &Ty, scope: &'a Scope, params: Vec<&'a str>) {
        let mut body = Body {
            f: None,
            default: Some(params),
            scope,
            listed: Vec::new(),
            unknown_effect: false,
            missing: Vec::new(),
            names: Vec::new(),
            params: 0,
            vars: Vec::new(),
        };
        let gives = self.expr(&mut body, value);
        self.expect(&body, value, &gives, needed);
    }

    /// Checks the defaults of the parameters of `f`, whose signature is
    /// `sig`: the parameters with one come after those without, and each
    /// default is as `default` requires.
    fn defaults(&mut self, f: &'a Function, sig: &'a Signature) {
        let names: Vec<&str> = f.params.iter().map(|p| p.name.as_str()).collect();
        let mut first = None;
        for (p, (_, ty)) in f.params.iter().zip(&sig.params) {
            match (&p.default, first) {
                (Some(value), _) => {
                    first = first.or(Some(p.name.as_str()));
                    self.default(value, ty, &sig.scope, names.clone());
                }
                (None, Some(q)) => self.errors.push(
                    Error::new(
                        p.pos,
                        format!(
                            "`{}` has no default, and comes after `{q}`, which has one",
                            p.name
                        ),
                    )
                    .with_fix(format!(
                        "move `{}` before `{q}`, or give it a default",
                        p.name
                    )),
                ),
                (None, None) => {}
            }
        }
    }

    /// Checks a list of effects: each named once, each one there is.
    fn effects(&mut self, effects: &[(String, Pos)]) {
        let mut listed: Vec<&str> = Vec::new();
        for (effect, pos) in effects {
            let effect = effect.as_str();
            if listed.contains(&effect) {
                self.errors.push(Error::new(
                    *pos,
                    format!("the `{effect}` effect is already listed"),
                ));
                continue;
            }
            listed.push(effect);
            if effect == "console" {
                continue;
            }
            if LIBRARY_EFFECTS.contains(&effect) || BUILT_IN_EFFECTS.contains(&effect) {
                self.errors
                    .push(Error::unsupported(*pos, &format!("the `{effect}` effect")));
            } else {
                self.errors.push(Error::new(
                    *pos,
                    format!("there is no effect named `{effect}`"),
                ));
            }
        }
    }

    /// Checks parameters, each named once in `f`, and the types of the
    /// parameters and the result, among the type parameters of `scope`.
    fn params(&mut self, f: &Function, scope: &Scope) {
        for (i, p) in f.params.iter().enumerate() {
            if f.params[..i].iter().any(|q| q.name == p.name) {
                self.errors.push(Error::new(
                    p.pos,
                    format!("`{}` is already defined in `{}`", p.name, f.name),
                ));
            }
            self.ty(&p.ty, scope);
        }
        if let Some(t) = &f.result {
            self.ty(t, scope);
        }
    }

    /// Checks the signature of function `f`: its type parameters, each
    /// named by a parameter's type, its types, and its list of effects.
    fn signature(&mut self, f: usize) {
        let defs = self.defs;
        let def = &defs.file.functions[f];
        if defs.function_names.get("main") != Some(&f) {
            self.generics(&def.generics, &format!("`{}`", def.name), Owner::Function);
            for g in &def.generics {
                let named = (def.params.iter())
                    .any(|p| type_names(&p.ty).iter().any(|(n, _)| *n == g.name));
                if !named {
                    self.errors.push(Error::unsupported(
                        g.pos,
                        "a type parameter that no parameter's type names",
                    ));
                }
            }
        }
        self.params(def, &defs.signatures[f].scope);
        self.defaults(def, &defs.signatures[f]);
        self.effects(&def.effects);
    }

    /// Checks a trait: its types, and the signature of each of its
    /// functions, whose parameters must name each type that is not `out`.
    fn declared_trait(&mut self, k: usize) {
        let defs = self.defs;
        let t = &defs.file.traits[k];
        self.generics(&t.generics, &format!("`{}`", t.name), Owner::Trait);
        for (i, f) in t.functions.iter().enumerate() {
            if t.functions[..i].iter().any(|g| g.name == f.name) {
                self.errors.push(Error::new(
                    f.pos,
                    format!("`{}` is already a function of `{}`", f.name, t.name),
                ));
                continue;
            }
            if let Some(g) = f.generics.first() {
                self.errors.push(Error::unsupported(
                    g.pos,
                    "a type parameter of a trait's function",
                ));
                continue;
            }
            self.params(f, &defs.trait_sigs[k][i].scope);
            self.defaults(f, &defs.trait_sigs[k][i]);
            self.effects(&f.effects);
            for p in &f.params {
                for (name, pos) in type_names(&p.ty) {
                    let out = t.generics.iter().any(|g| g.name == name && g.out.is_some());
                    if out {
                        self.errors.push(Error::unsupported(
                            pos,
                            "an `out` type in a parameter of a trait's function",
                        ));
                    }
                }
            }
            for g in t.generics.iter().filter(|g| g.out.is_none()) {
                let named =
                    (f.params.iter()).any(|p| type_names(&p.ty).iter().any(|(n, _)| *n == g.name));
                if !named {
                    self.errors.push(Error::unsupported(
                        f.pos,
                        &format!(
                            "a function of a trait whose parameters' types do not name `{}`",
                            g.name
                        ),
                    ));
                }
            }
        }
    }

    /// Checks an implementation: its trait, its types, that it is for types
    /// no earlier one is for, and that it has each of the trait's functions
    /// once, with the trait's signature, and no other function.
    fn implementation(&mut self, i: usize) {
        let defs = self.defs;
        let im = &defs.file.impls[i];
        let name = im.name.as_str();
        let Some(&k) = defs.trait_names.get(name) else {
            self.errors.push(if TRAITS_NOT_BUILT.contains(&name) {
                Error::unsupported(im.pos, &format!("implementing `{name}`"))
            } else if defs.struct_names.contains_key(name)
                || name == TEXT
                || TYPES_NOT_BUILT.contains(&name)
            {
                Error::new(im.pos, format!("`{name}` is not a trait"))
            } else {
                Error::new(im.pos, format!("there is no trait named `{name}`"))
            });
            return;
        };
        let t = &defs.file.traits[k];
        self.generics(&im.generics, "this implementation", Owner::Impl);
        let scope = defs.scope(&im.generics);
        for a in &im.args {
            self.ty(a, &scope);
        }
        if im.args.len() != t.generics.len() {
            self.errors.push(Error::new(
                im.pos,
                format!(
                    "`{name}` takes {}, and this gives {}",
                    count(t.generics.len(), "type"),
                    im.args.len()
                ),
            ));
            return;
        }
        let Some(info) = &defs.impls[i] else {
            return;
        };
        let mut chosen = true;
        for g in &im.generics {
            let named = (im.args.iter().zip(&t.generics))
                .filter(|(_, tg)| tg.out.is_none())
                .any(|(a, _)| type_names(a).iter().any(|(n, _)| *n == g.name));
            if !named {
                chosen = false;
                self.errors.push(Error::unsupported(
                    g.pos,
                    "a type parameter of an implementation that its types other than `out` ones \
                     do not name",
                ));
            }
        }
        if chosen && !info.chosen {
            self.errors.push(Error::new(
                im.pos,
                format!(
                    "`{name}` is already implemented for {}",
                    defs.types_of(k, &info.args, &info.scope)
                ),
            ));
        }
        for (j, f) in im.functions.iter().enumerate() {
            if im.functions[..j].iter().any(|g| g.name == f.name) {
                self.errors.push(Error::new(
                    f.pos,
                    format!("`{}` is already in this implementation", f.name),
                ));
                continue;
            }
            match t.functions.iter().position(|g| g.name == f.name) {
                Some(fi) => self.implemented(i, fi, f),
                None => self.errors.push(Error::new(
                    f.pos,
                    format!("`{}` is not a function of `{name}`", f.name),
                )),
            }
        }
        let lacking: Vec<&str> = (t.functions.iter().zip(&info.fns))
            .filter(|(_, at)| at.is_none())
            .map(|(f, _)| f.name.as_str())
            .collect();
        if !lacking.is_empty() {
            self.errors.push(Error::new(
                im.pos,
                format!("this implementation of `{name}` lacks {}", names(&lacking)),
            ));
        }
    }

    /// Checks that function `f` of implementation `i` has the signature of
    /// the trait's function `fi`, for the implementation's types.
    fn implemented(&mut self, i: usize, fi: usize, f: &Function) {
        let defs = self.defs;
        let info = defs.impls[i].as_ref().expect("known");
        let trait_fn = &defs.file.traits[info.of].functions[fi];
        let sig = &defs.trait_sigs[info.of][fi];
        let scope = &info.scope;
        let trait_name = &defs.file.traits[info.of].name;
        for p in &f.params {
            if let Some(value) = &p.default {
                self.errors.push(
                    Error::new(
                        value.pos(),
                        format!(
                            "the defaults of `{}` are stated in `{trait_name}`, and an \
                             implementation does not repeat them",
                            f.name
                        ),
                    )
                    .with_fix(format!("remove the default of `{}`", p.name)),
                );
            }
        }
        let before = self.errors.len();
        self.params(f, scope);
        self.effects(&f.effects);
        if self.errors.len() > before {
            return;
        }
        let wanted: Vec<(String, Ty)> = (sig.params.iter())
            .map(|(n, t)| (n.clone(), t.subst(&info.args)))
            .collect();
        let gives = sig.gives.subst(&info.args);
        let written: Vec<(String, Ty)> = (f.params.iter())
            .map(|p| (p.name.clone(), defs.quietly(&p.ty, scope)))
            .collect();
        let written_gives = match &f.result {
            Some(t) => defs.quietly(t, scope),
            None => Ty::Nothing,
        };
        fn set(effects: &[(String, Pos)]) -> Vec<&str> {
            let mut set: Vec<&str> = effects.iter().map(|(e, _)| e.as_str()).collect();
            set.sort_unstable();
            set.dedup();
            set
        }
        let same = f.generics.is_empty()
            && written == wanted
            && written_gives == gives
            && set(&f.effects) == set(&trait_fn.effects);
        if same {
            return;
        }
        let params: Vec<String> = (wanted.iter())
            .map(|(n, t)| format!("{n}: {}", defs.text(t, scope)))
            .collect();
        let mut text = format!("fn {}({})", f.name, params.join(", "));
        if gives != Ty::Nothing {
            text.push_str(&format!(" -> {}", defs.text(&gives, scope)));
        }
        let effects = set(&trait_fn.effects);
        if !effects.is_empty() {
            text.push_str(&format!(" ! {}", effects.join(", ")));
        }
        self.errors.push(
            Error::new(
                f.pos,
                format!("`{}` differs from its signature in `{trait_name}`", f.name),
            )
            .with_fix(format!("write `{text}`")),
        );
    }

    /// Reports a value that gives `given` where `needed` is needed.
    fn expect(&mut self, body: &Body, value: &Expr, given: &Ty, needed: &Ty) {
        let defs = self.defs;
        match (given, needed) {
            (_, Ty::Unknown | Ty::Nothing) | (Ty::Unknown, _) => {}
            (Ty::Nothing, _) => {
                let needed = defs.describe(needed, body.scope);
                self.errors.push(self.nothing_where(value, &needed));
            }
            (given, needed) if given != needed => self.errors.push(Error::new(
                value.pos(),
                format!(
                    "this gives {}, where {} is needed",
                    defs.describe(given, body.scope),
                    defs.describe(needed, body.scope)
                ),
            )),
            _ => {}
        }
    }

    /// Reports a value that gives `given` where a value of `pattern` is
    /// needed, whose type parameters, those of `vars`, stand for the types
    /// in `sol`, and fills in `sol`.
    fn fit(
        &mut self,
        body: &Body,
        value: &Expr,
        given: &Ty,
        pattern: &Ty,
        sol: &mut [Option<Ty>],
        vars: &Scope,
    ) {
        let defs = self.defs;
        match (given, pattern) {
            (_, Ty::Unknown | Ty::Nothing) | (Ty::Unknown, _) => {}
            (Ty::Nothing, _) => {
                let needed = defs.describe_pattern(pattern, sol, vars, body.scope);
                self.errors.push(self.nothing_where(value, &needed));
            }
            (given, pattern) => {
                let needed = defs.describe_pattern(pattern, sol, vars, body.scope);
                if !unify(pattern, given, sol) {
                    self.errors.push(Error::new(
                        value.pos(),
                        format!(
                            "this gives {}, where {needed} is needed",
                            defs.describe(given, body.scope)
                        ),
                    ));
                }
            }
        }
    }

    fn nothing_where(&self, value: &Expr, needed: &str) -> Error {
        let what = match value {
            Expr::Call(call) => format!("`{}` returns nothing", call.callee),
            _ => "this gives nothing".to_string(),
        };
        Error::new(value.pos(), format!("{what}, where {needed} is needed"))
    }

    fn body(&mut self, template: Template) {
        let defs = self.defs;
        let view = defs.view(template);
        let f = view.f;
        let mut listed = Vec::new();
        let mut unknown_effect = false;
        for (effect, _) in view.effects {
            let effect = effect.as_str();
            if !listed.contains(&effect) {
                listed.push(effect);
                unknown_effect |= effect != "console"
                    && !LIBRARY_EFFECTS.contains(&effect)
                    && !BUILT_IN_EFFECTS.contains(&effect);
            }
        }
        let mut body = Body {
            f: Some(f),
            default: None,
            scope: view.scope,
            listed,
            unknown_effect,
            missing: Vec::new(),
            names: view.params.clone(),
            params: view.params.len(),
            vars: Vec::new(),
        };
        for (i, stmt) in f.body.iter().enumerate() {
            let last = i + 1 == f.body.len();
            let (gives, value) = match stmt {
                Stmt::Define {
                    name,
                    pos,
                    ty,
                    var,
                    value,
                } => {
                    let gives = self.expr(&mut body, value);
                    let defined = body.names.iter().position(|(n, _)| n == name);
                    match (defined, ty, var) {
                        (Some(at), None, false) => {
                            if body.vars.contains(name) {
                                let needed = body.names[at].1.clone();
                                self.expect(&body, value, &gives, &needed);
                            } else {
                                self.errors.push(not_a_var(&body, name, *pos, at));
                            }
                        }
                        _ => {
                            let bound = match ty {
                                Some(t) => {
                                    self.ty(t, view.scope);
                                    let needed = defs.quietly(t, view.scope);
                                    self.expect(&body, value, &gives, &needed);
                                    needed
                                }
                                None => {
                                    if gives == Ty::Nothing {
                                        self.errors.push(Error::unsupported(
                                            value.pos(),
                                            "binding nothing",
                                        ));
                                    }
                                    gives
                                }
                            };
                            if defined.is_some() {
                                self.errors.push(Error::new(
                                    *pos,
                                    format!("`{name}` is already defined in `{}`", f.name),
                                ));
                            } else {
                                body.names.push((name.clone(), bound));
                                if *var {
                                    body.vars.push(name.clone());
                                }
                            }
                        }
                    }
                    if let (true, Some(t)) = (last, &view.result) {
                        self.ends_with_nothing(f, *pos, t);
                    }
                    continue;
                }
                Stmt::Assign {
                    name,
                    pos,
                    fields,
                    value,
                } => {
                    let gives = self.expr(&mut body, value);
                    if let Some(needed) = self.place(&body, name, *pos, fields) {
                        self.expect(&body, value, &gives, &needed);
                    }
                    if let (true, Some(t)) = (last, &view.result) {
                        self.ends_with_nothing(f, *pos, t);
                    }
                    continue;
                }
                Stmt::Expr(value) => (self.expr(&mut body, value), value),
            };
            if !last {
                continue;
            }
            match (&gives, &view.result) {
                (_, Some(_)) => self.expect(&body, value, &gives, &view.gives),
                (Ty::Text | Ty::Struct(..) | Ty::Param(_), None) => self.errors.push(Error::new(
                    value.pos(),
                    format!(
                        "`{}` returns nothing, and its body ends with {}",
                        f.name,
                        defs.describe(&gives, view.scope)
                    ),
                )),
                _ => {}
            }
        }
        if let (true, Some(t)) = (f.body.is_empty(), &view.result) {
            self.errors.push(Error::new(
                f.end,
                format!(
                    "`{}` returns `{t}`, and its body is empty, which gives nothing",
                    f.name
                ),
            ));
        }
    }

    /// Reports a body that ends with a statement that gives nothing, the
    /// one at `pos`, where `f` returns the type written `t`.
    fn ends_with_nothing(&mut self, f: &Function, pos: Pos, t: &str) {
        self.errors.push(Error::new(
            pos,
            format!(
                "`{}` returns `{t}`, and its body ends with a binding or an assignment, \
                 which gives nothing",
                f.name
            ),
        ));
    }

    /// Checks a field of a `var`, or a field of one of its fields, as a
    /// place to assign to, and gives the type of what it holds. Gives
    /// `None` when the place holds a mistake, which is reported.
    fn place(&mut self, body: &Body, name: &str, pos: Pos, fields: &[(String, Pos)]) -> Option<Ty> {
        let defs = self.defs;
        let Some(at) = body.names.iter().position(|(n, _)| n == name) else {
            self.errors.push(Error::new(
                pos,
                format!(
                    "there is no name `{name}` defined before this in `{}`",
                    body.owner().name
                ),
            ));
            return None;
        };
        if !body.vars.iter().any(|v| v == name) {
            self.errors.push(not_a_var(body, name, pos, at));
            return None;
        }
        let mut ty = body.names[at].1.clone();
        for (field, pos) in fields {
            ty = match ty {
                Ty::Struct(s, args) => {
                    let def = &defs.file.structs[s];
                    match def.fields.iter().position(|f| &f.name == field) {
                        Some(at) => defs.fields[s][at].subst(&args),
                        None => {
                            self.errors.push(Error::new(
                                *pos,
                                format!("`{}` has no field named `{field}`", def.name),
                            ));
                            return None;
                        }
                    }
                }
                Ty::Unknown => return None,
                given => {
                    self.errors.push(Error::new(
                        *pos,
                        format!(
                            "the field `{field}` is assigned in {}, which is not a struct",
                            defs.describe(&given, body.scope)
                        ),
                    ));
                    return None;
                }
            };
        }
        Some(ty)
    }

    fn expr(&mut self, body: &mut Body<'a>, expr: &Expr) -> Ty {
        let defs = self.defs;
        match expr {
            Expr::Text { .. } => Ty::Text,
            Expr::Name { name, pos } => {
                if let Some((_, ty)) = body.names.iter().find(|(n, _)| n == name) {
                    ty.clone()
                } else {
                    let message = match &body.default {
                        Some(params) if params.contains(&name.as_str()) => {
                            format!("a default uses no parameter, and `{name}` is one")
                        }
                        Some(_) => format!("there is no name `{name}` for a default to use"),
                        None => format!(
                            "there is no name `{name}` defined before this in `{}`",
                            body.owner().name
                        ),
                    };
                    self.errors.push(Error::new(*pos, message));
                    Ty::Unknown
                }
            }
            Expr::Call(call) => self.call(body, call),
            Expr::Field { of, name, pos } => match self.expr(body, of) {
                Ty::Struct(s, args) => {
                    let def = &defs.file.structs[s];
                    match def.fields.iter().position(|f| &f.name == name) {
                        Some(at) => defs.fields[s][at].subst(&args),
                        None => {
                            self.errors.push(Error::new(
                                *pos,
                                format!("`{}` has no field named `{name}`", def.name),
                            ));
                            Ty::Unknown
                        }
                    }
                }
                Ty::Unknown => Ty::Unknown,
                given => {
                    self.errors.push(Error::new(
                        *pos,
                        format!(
                            "the field `{name}` is read from {}, which is not a struct",
                            defs.describe(&given, body.scope)
                        ),
                    ));
                    Ty::Unknown
                }
            },
        }
    }

    /// Checks the arguments of a call that names nothing there is, for the
    /// mistakes in them.
    fn unknown_call(&mut self, body: &mut Body<'a>, call: &Call) -> Ty {
        for arg in &call.args {
            self.expr(body, &arg.value);
        }
        Ty::Unknown
    }

    fn call(&mut self, body: &mut Body<'a>, call: &Call) -> Ty {
        let defs = self.defs;
        let name = call.callee.as_str();
        if let Some((q, at)) = &call.qualifier {
            let Some(&k) = defs.trait_names.get(q.as_str()) else {
                self.errors.push(if body.names.iter().any(|(n, _)| n == q) {
                    Error::unsupported(call.name_pos, "calling what a field gives")
                } else {
                    Error::new(*at, format!("there is no trait named `{q}`"))
                });
                return self.unknown_call(body, call);
            };
            let t = &defs.file.traits[k];
            let Some(fi) = t.functions.iter().position(|f| f.name == name) else {
                self.errors.push(Error::new(
                    call.name_pos,
                    format!("`{q}` has no function named `{name}`"),
                ));
                return self.unknown_call(body, call);
            };
            return self.trait_call(body, call, k, fi);
        }
        if let Some(&s) = defs.struct_names.get(name) {
            return self.construction(body, call, s);
        }
        if name == "print" {
            let print = Signature::print();
            if self.effect_in_default(body, call, &print) {
                return self.unknown_call(body, call);
            }
            self.arguments(body, call, &print, true);
            return Ty::Nothing;
        }
        if let Some(&f) = defs.function_names.get(name) {
            let sig = &defs.signatures[f];
            if self.effect_in_default(body, call, sig) {
                return self.unknown_call(body, call);
            }
            let sol = self.arguments(body, call, sig, false);
            for (g, bound) in sig.scope.bounds.iter().enumerate() {
                if let (Bound::Trait(k), Some(t)) = (bound, &sol[g])
                    && let Found::No = defs.meets(*k, t, body.scope, &mut Vec::new())
                {
                    self.errors.push(Error::new(
                        call.pos,
                        format!(
                            "there is no implementation of `{}` for `{}`, which `{name}` needs",
                            defs.file.traits[*k].name,
                            defs.text(t, body.scope)
                        ),
                    ));
                }
            }
            return sig.gives.solved(&sol);
        }
        match defs.trait_fns.get(name).map(Vec::as_slice) {
            Some([(k, fi)]) => self.trait_call(body, call, *k, *fi),
            Some(of) if !of.is_empty() => {
                let traits: Vec<&str> = (of.iter())
                    .map(|(k, _)| defs.file.traits[*k].name.as_str())
                    .collect();
                let fixes: Vec<String> = traits.iter().map(|t| format!("`{t}.{name}`")).collect();
                self.errors.push(
                    Error::new(
                        call.pos,
                        format!("`{name}` is a function of {}", names(&traits)),
                    )
                    .with_fix(format!("write {}", fixes.join(" or "))),
                );
                self.unknown_call(body, call)
            }
            _ => {
                self.errors.push(Error::new(
                    call.pos,
                    format!("there is no function named `{name}`"),
                ));
                self.unknown_call(body, call)
            }
        }
    }

    /// Checks a call of function `fi` of trait `k`, which runs the
    /// implementation its types choose.
    fn trait_call(&mut self, body: &mut Body<'a>, call: &Call, k: usize, fi: usize) -> Ty {
        let defs = self.defs;
        let sig = &defs.trait_sigs[k][fi];
        if self.effect_in_default(body, call, sig) {
            return self.unknown_call(body, call);
        }
        let sol = self.arguments(body, call, sig, false);
        let outs = &defs.outs[k];
        if (sol.iter().zip(outs)).any(|(t, out)| !out && t.is_none()) {
            return Ty::Unknown;
        }
        let types: Vec<Ty> = sol
            .iter()
            .map(|t| t.clone().unwrap_or(Ty::Unknown))
            .collect();
        match defs.find(k, &types, body.scope, &mut Vec::new()) {
            Found::Yes(all, _) => sig.gives.subst(&all),
            Found::No => {
                self.errors.push(Error::new(
                    call.pos,
                    format!(
                        "there is no implementation of `{}` for {}",
                        defs.file.traits[k].name,
                        defs.types_of(k, &types, body.scope)
                    ),
                ));
                Ty::Unknown
            }
            Found::Unknown => Ty::Unknown,
        }
    }

    /// Reports a call in a default of a function with this signature, if it
    /// carries an effect, and gives whether it does.
    fn effect_in_default(&mut self, body: &Body, call: &Call, signature: &Signature) -> bool {
        let (Some(_), Some(effect)) = (&body.default, signature.effects.first()) else {
            return false;
        };
        self.errors.push(Error::new(
            call.pos,
            format!(
                "a default carries no effect, and `{}` needs the `{effect}` effect",
                call.callee
            ),
        ));
        true
    }

    /// Checks the construction of struct `s`, which gives each of its
    /// fields once by name, in any order, except those it leaves to their
    /// defaults.
    fn construction(&mut self, body: &mut Body<'a>, call: &Call, s: usize) -> Ty {
        let defs = self.defs;
        let def = &defs.file.structs[s];
        let vars = &defs.struct_scopes[s];
        let mut sol = vec![None; def.generics.len()];
        let mut given = vec![false; def.fields.len()];
        let mut by_position = false;
        for arg in &call.args {
            let gives = self.expr(body, &arg.value);
            match &arg.name {
                None => {
                    if !by_position {
                        let mut error = Error::new(
                            arg.value.pos(),
                            format!("the fields of `{}` are given by name", def.name),
                        );
                        if let Some(at) = given.iter().position(|g| !g) {
                            error = error
                                .with_fix(format!("write `{}: ` before it", def.fields[at].name));
                        }
                        self.errors.push(error);
                    }
                    by_position = true;
                }
                Some((name, pos)) => match def.fields.iter().position(|f| &f.name == name) {
                    Some(i) if given[i] => self.errors.push(Error::new(
                        *pos,
                        format!("the `{name}` field is given twice"),
                    )),
                    Some(i) => {
                        given[i] = true;
                        let pattern = &defs.fields[s][i];
                        self.fit(body, &arg.value, &gives, pattern, &mut sol, vars);
                    }
                    None => self.errors.push(Error::new(
                        *pos,
                        format!("`{}` has no field named `{name}`", def.name),
                    )),
                },
            }
        }
        let missing: Vec<&str> = (def.fields.iter())
            .zip(&given)
            .filter(|(f, given)| !**given && f.default.is_none())
            .map(|(f, _)| f.name.as_str())
            .collect();
        // A field given by position may be one of those missing.
        if !missing.is_empty() && !by_position {
            self.errors.push(Error::new(
                call.end,
                format!("`{}` needs a value for {}", def.name, names(&missing)),
            ));
        }
        if defs.cyclic[s] {
            return Ty::Unknown;
        }
        let args = sol.into_iter().map(|t| t.unwrap_or(Ty::Unknown)).collect();
        Ty::structure(s, args)
    }

    /// Checks the effects and the arguments of a call of a function with
    /// this signature, and gives the type each of its type parameters
    /// stands for, where the arguments say.
    fn arguments(
        &mut self,
        body: &mut Body<'a>,
        call: &Call,
        signature: &Signature,
        is_print: bool,
    ) -> Vec<Option<Ty>> {
        let vars = &signature.scope;
        let mut sol: Vec<Option<Ty>> = vec![None; vars.names.len()];
        for effect in &signature.effects {
            if body.listed.contains(&effect.as_str())
                || body.unknown_effect
                || body.missing.contains(effect)
            {
                continue;
            }
            body.missing.push(effect.clone());
            let mut effects = body.listed.clone();
            effects.extend(body.missing.iter().map(String::as_str));
            self.errors.push(
                Error::new(
                    call.pos,
                    format!(
                        "`{}` needs the `{effect}` effect, which `{}` does not declare",
                        call.callee,
                        body.owner().name
                    ),
                )
                .with_fix(format!("write `{}`", written(body.owner(), &effects))),
            );
        }
        let mut given: Vec<bool> = vec![false; signature.params.len()];
        let mut named = false;
        let mut too_many = false;
        let mut next = 0;
        for arg in &call.args {
            // An argument `print` does not take yet is not looked into.
            let mut look_into = true;
            // The parameter the argument is for, if it is for one.
            let mut param = None;
            match &arg.name {
                None if named => self.errors.push(
                    Error::new(
                        arg.value.pos(),
                        "an argument by position comes after a named one",
                    )
                    .with_fix("name it, or move it before the named arguments"),
                ),
                None if next < given.len() => {
                    given[next] = true;
                    param = Some(next);
                    next += 1;
                }
                None if is_print => {
                    look_into = false;
                    if !too_many {
                        self.errors.push(Error::unsupported(
                            arg.value.pos(),
                            "the `end` and `to` arguments of `print`",
                        ));
                    }
                    too_many = true;
                }
                None => {
                    if !too_many {
                        self.errors.push(Error::new(
                            arg.value.pos(),
                            format!(
                                "`{}` takes {} arguments, and this is one more",
                                call.callee,
                                given.len()
                            ),
                        ));
                    }
                    too_many = true;
                }
                Some((name, pos)) => {
                    named = true;
                    match signature.params.iter().position(|(p, _)| p == name) {
                        Some(i) if given[i] => self.errors.push(Error::new(
                            *pos,
                            format!("the `{name}` argument is given twice"),
                        )),
                        Some(i) => {
                            given[i] = true;
                            param = Some(i);
                        }
                        None if is_print && (name == "end" || name == "to") => {
                            look_into = false;
                            self.errors.push(Error::unsupported(
                                *pos,
                                "the `end` and `to` arguments of `print`",
                            ));
                        }
                        None => self.errors.push(Error::new(
                            *pos,
                            format!("`{}` has no parameter named `{name}`", call.callee),
                        )),
                    }
                }
            }
            if !look_into {
                continue;
            }
            let gives = self.expr(body, &arg.value);
            match param {
                Some(i) => {
                    let pattern = &signature.params[i].1;
                    self.fit(body, &arg.value, &gives, pattern, &mut sol, vars);
                }
                None if gives == Ty::Nothing => {
                    self.errors.push(self.nothing_where(&arg.value, "text"));
                }
                None => {}
            }
        }
        let missing: Vec<&str> = (signature.params.iter())
            .zip(given.iter().zip(&signature.defaults))
            .filter(|(_, (given, default))| !**given && !**default)
            .map(|((p, _), _)| p.as_str())
            .collect();
        if is_print && !missing.is_empty() {
            self.errors.push(
                Error::new(call.end, "`print` needs the text to write")
                    .with_fix("write `print(\"hello\")`"),
            );
        } else if !missing.is_empty() {
            self.errors.push(Error::new(
                call.end,
                format!(
                    "`{}` needs an argument for {}",
                    call.callee,
                    names(&missing)
                ),
            ));
        }
        sol
    }

    /// Reports each call that closes a cycle of calls between functions
    /// written on their own, in a walk of the calls from `f`. `path` is the
    /// functions being walked, and `done` those already walked.
    ///
    /// The calls in a default that a call leaves out are calls of the
    /// function the call is written in, as the default is filled in there.
    fn recursion(&mut self, f: usize, path: &mut Vec<usize>, done: &mut Vec<usize>) {
        path.push(f);
        let defs = self.defs;
        let mut all = Vec::new();
        let mut cycles = Vec::new();
        for call in calls(values(&defs.file.functions[f].body)) {
            all.push(call);
            defs.defaults_called(call, &mut Vec::new(), &mut all, &mut cycles);
        }
        self.errors.extend(cycles.into_iter().map(default_cycle));
        for call in all {
            if call.qualifier.is_some() {
                continue;
            }
            let Some(&callee) = self.defs.function_names.get(call.callee.as_str()) else {
                continue;
            };
            if path.contains(&callee) {
                self.errors.push(Error::unsupported(
                    call.pos,
                    "a function that calls itself, directly or through others,",
                ));
            } else if !done.contains(&callee) {
                self.recursion(callee, path, done);
            }
        }
        path.pop();
        done.push(f);
    }

    /// Reports each call that would need a copy of a function without end:
    /// one already being walked, or one of a function being walked for
    /// larger types, which would grow forever. `path` is the copies being
    /// walked from `main`, and `done` those already walked.
    fn copies(&mut self, instance: &Instance, path: &mut Vec<Instance>, done: &mut Vec<Instance>) {
        path.push(instance.clone());
        let (calls, cycles) = self.defs.calls_of(instance);
        self.errors.extend(cycles.into_iter().map(default_cycle));
        for (pos, callee) in calls {
            let size = |i: &Instance| i.args.iter().map(Ty::size).sum::<usize>();
            let endless = path.iter().any(|p| {
                p.template == callee.template && (*p == callee || size(&callee) > size(p))
            });
            if endless {
                self.errors.push(Error::unsupported(
                    pos,
                    "a function that calls itself, directly or through others,",
                ));
            } else if !done.contains(&callee) {
                self.copies(&callee, path, done);
            }
        }
        path.pop();
        done.push(instance.clone());
    }
}

/// Lowers checked functions to the form every target receives, one copy
/// of a function for each choice of its type parameters that runs.
struct Lowering<'a> {
    defs: &'a Defs<'a>,
    /// The copies, each at its place in the program, in the order they are
    /// first called.
    instances: Vec<Instance>,
    texts: Vec<Vec<u8>>,
}

/// A function being lowered.
struct Lowered {
    /// What each name defined so far gives.
    types: Vec<(String, Ty)>,
    /// The texts each name defined so far stands for.
    values: Vec<Vec<Value>>,
    locals: usize,
    steps: Vec<Step>,
}

impl<'a> Lowering<'a> {
    fn text(&mut self, text: &[u8]) -> Value {
        let at = match self.texts.iter().position(|t| t == text) {
            Some(at) => at,
            None => {
                self.texts.push(text.to_vec());
                self.texts.len() - 1
            }
        };
        Value::Text(at)
    }

    /// How many texts a value of the type is lowered to.
    fn width(&self, ty: &Ty) -> usize {
        match ty {
            Ty::Text => 1,
            Ty::Struct(s, args) => (self.defs.fields[*s].iter())
                .map(|f| self.width(&f.subst(args)))
                .sum(),
            Ty::Param(_) | Ty::Nothing | Ty::Unknown => 0,
        }
    }

    /// Where the texts of a field, or of a field of a field for each
    /// further name, are among the texts of a value of the type, and how
    /// many there are.
    fn offset(&self, ty: &Ty, fields: &[(String, Pos)]) -> (usize, usize) {
        let mut ty = ty.clone();
        let mut start = 0;
        for (name, _) in fields {
            let Ty::Struct(s, args) = ty else {
                unreachable!("checked")
            };
            let defs = self.defs;
            let mut found = None;
            for (field, t) in defs.file.structs[s].fields.iter().zip(&defs.fields[s]) {
                let t = t.subst(&args);
                if &field.name == name {
                    found = Some(t);
                    break;
                }
                start += self.width(&t);
            }
            ty = found.expect("checked");
        }
        (start, self.width(&ty))
    }

    /// New locals for a value of the type.
    fn locals(&self, out: &mut Lowered, ty: &Ty) -> Vec<usize> {
        let width = self.width(ty);
        out.locals += width;
        (out.locals - width..out.locals).collect()
    }

    /// The place of a copy in the program, which it takes if it has none.
    fn place(&mut self, instance: Instance) -> usize {
        match self.instances.iter().position(|i| *i == instance) {
            Some(at) => at,
            None => {
                self.instances.push(instance);
                self.instances.len() - 1
            }
        }
    }

    fn function(&mut self, instance: &Instance) -> ir::Function {
        let defs = self.defs;
        let view = defs.view(instance.template);
        let mut out = Lowered {
            types: Vec::new(),
            values: Vec::new(),
            locals: 0,
            steps: Vec::new(),
        };
        for (name, ty) in &view.params {
            let ty = ty.subst(&instance.args);
            let locals = self.locals(&mut out, &ty);
            out.types.push((name.clone(), ty));
            out.values
                .push(locals.into_iter().map(Value::Local).collect());
        }
        let params = out.locals;
        let mut result = Vec::new();
        for stmt in &view.f.body {
            result = match stmt {
                Stmt::Define { name, value, .. } => {
                    // A name stands for the texts it is bound to, so a
                    // binding needs no step of its own. A body runs its
                    // statements once each, in order, so an assignment
                    // makes the name stand for the new texts from then on,
                    // and a name bound to the old ones keeps them.
                    let (ty, value) = self.expr(&mut out, value);
                    match out.types.iter().position(|(n, _)| n == name) {
                        Some(at) => out.values[at] = value,
                        None => {
                            out.types.push((name.clone(), ty));
                            out.values.push(value);
                        }
                    }
                    Vec::new()
                }
                Stmt::Assign {
                    name,
                    fields,
                    value,
                    ..
                } => {
                    let (_, value) = self.expr(&mut out, value);
                    let at = out.types.iter().position(|(n, _)| n == name);
                    let at = at.expect("checked");
                    let (start, width) = self.offset(&out.types[at].1, fields);
                    out.values[at].splice(start..start + width, value);
                    Vec::new()
                }
                Stmt::Expr(value) => self.expr(&mut out, value).1,
            };
        }
        if view.gives == Ty::Nothing {
            result.clear();
        }
        ir::Function {
            params,
            locals: out.locals,
            steps: out.steps,
            results: result,
        }
    }

    /// The texts an expression gives, and their type.
    fn expr(&mut self, out: &mut Lowered, expr: &Expr) -> (Ty, Vec<Value>) {
        let defs = self.defs;
        match expr {
            Expr::Text { text, .. } => (Ty::Text, vec![self.text(text)]),
            Expr::Name { name, .. } => {
                let at = out.types.iter().rposition(|(n, _)| n == name);
                let at = at.expect("checked");
                (out.types[at].1.clone(), out.values[at].clone())
            }
            Expr::Call(call) => match defs.callee(call, &out.types) {
                Callee::Construct(s, args) => {
                    let values = self.construction(out, call, s);
                    (Ty::Struct(s, args), values)
                }
                Callee::Print => {
                    let (_, text) = self.expr(out, &call.args[0].value);
                    let newline = self.text(b"\n");
                    for value in [text[0], newline] {
                        out.steps.push(Step::Write {
                            stream: Stream::Stdout,
                            value,
                        });
                    }
                    (Ty::Nothing, Vec::new())
                }
                Callee::Run(instance, gives) => {
                    let params = defs.view(instance.template).params;
                    let function = self.place(instance);
                    // Arguments are worked out in the order they are
                    // written, and then passed in the order of the
                    // parameters.
                    let mut args: Vec<Vec<Value>> = vec![Vec::new(); params.len()];
                    for (arg, at) in call.args.iter().zip(params_of(&params, &call.args)) {
                        args[at] = self.expr(out, &arg.value).1;
                    }
                    // Then the defaults it leaves out, in the order of the
                    // parameters, as if they were written after the rest.
                    for (at, default) in defs.left_out(call) {
                        args[at] = self.expr(out, default).1;
                    }
                    let results = self.locals(out, &gives);
                    out.steps.push(Step::Call {
                        function,
                        args: args.concat(),
                        results: results.clone(),
                    });
                    (gives, results.into_iter().map(Value::Local).collect())
                }
            },
            Expr::Field { of, name, .. } => {
                let (Ty::Struct(s, args), values) = self.expr(out, of) else {
                    unreachable!("checked")
                };
                let mut at = 0;
                for (field, ty) in defs.file.structs[s].fields.iter().zip(&defs.fields[s]) {
                    let ty = ty.subst(&args);
                    let width = self.width(&ty);
                    if &field.name == name {
                        return (ty, values[at..at + width].to_vec());
                    }
                    at += width;
                }
                unreachable!("checked")
            }
        }
    }

    /// Works out the fields in the order they are written, and then the
    /// defaults of those left out in the order the struct declares them,
    /// and gives their texts in the order the struct declares them.
    fn construction(&mut self, out: &mut Lowered, call: &Call, s: usize) -> Vec<Value> {
        let defs = self.defs;
        let def = &defs.file.structs[s];
        let mut fields: Vec<Vec<Value>> = vec![Vec::new(); def.fields.len()];
        for Arg { name, value } in &call.args {
            let (_, value) = self.expr(out, value);
            let (name, _) = name.as_ref().expect("checked");
            let at = def.fields.iter().position(|f| &f.name == name);
            fields[at.expect("checked")] = value;
        }
        for (at, default) in defs.left_out(call) {
            fields[at] = self.expr(out, default).1;
        }
        fields.concat()
    }
}
