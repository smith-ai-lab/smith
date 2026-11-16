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
//! left when the program runs. A struct is lowered to its fields, and a
//! tuple to its elements, one after another, until only texts are left, and
//! an enum to which case it holds, followed by the texts of that case, in as
//! many places as its largest case takes. The form every target receives
//! holds texts and cases alone.
//!
//! The type of each expression is recorded as it is checked, among the
//! type parameters of the definition it is written in, so lowering a copy
//! of a function reads the types of its expressions with its type
//! parameters replaced.
//!
//! A default is filled in where the call or construction that leaves it out
//! is written, so it is lowered as if it were written there, and no copy of
//! a function knows which of its arguments were left out.

use std::collections::{HashMap, HashSet};

use crate::diag::{Error, Pos};
use crate::ir::{self, Step, Stream, Value};
use crate::parser::{
    Arg, Arm, Call, Expr, File, Function, Generic, Lambda, Match, Part, Pattern, Stmt, Type,
};

/// The effects the standard library declares, other than `console`.
const LIBRARY_EFFECTS: &[&str] = &["fs", "net", "time", "ui"];

/// The effects the compiler builds.
const BUILT: &[&str] = &["console", "alloc"];

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
    "Iterate",
    "FromInt",
    "FromFloat",
    "FromString",
    "FromArray",
];

/// The traits the prelude defines that the compiler builds, with their
/// implementations, written in Smith. They are read at line 0, which no file
/// has, so nothing in a file can be confused with them.
const PRELUDE: &str = "\
trait Fmt[T] {
  fn fmt(value: T) -> str ! alloc
}

impl Fmt[str] {
  fn fmt(value: str) -> str ! alloc { value }
}
";

/// The names of the traits `PRELUDE` defines.
const PRELUDE_TRAITS: &[&str] = &["Fmt"];

/// Whether a definition is the prelude's.
fn of_prelude(pos: Pos) -> bool {
    pos.line == 0
}

/// The file with the definitions of `PRELUDE` before its own.
fn with_prelude(mut file: File) -> File {
    let mut tokens = crate::lexer::lex(PRELUDE.as_bytes()).expect("the prelude lexes");
    for t in &mut tokens {
        t.pos.line = 0;
    }
    let prelude = crate::parser::parse(&tokens).expect("the prelude parses");
    file.traits.splice(0..0, prelude.traits);
    file.impls.splice(0..0, prelude.impls);
    file
}

/// What an expression gives.
#[derive(Clone, Debug, PartialEq, Eq)]
enum Ty {
    Text,
    /// A struct, by its place in the file's structs, with a type for each
    /// of its type parameters.
    Struct(usize, Vec<Ty>),
    /// An enum, by its place in the file's enums, with a type for each of
    /// its type parameters.
    Enum(usize, Vec<Ty>),
    /// A tuple of two values or more, with the type of each.
    Tuple(Vec<Ty>),
    /// A function: the types of its parameters, followed by the type it
    /// gives.
    Fn(Vec<Ty>),
    /// A type parameter of the function, the implementation, the struct,
    /// or the trait the type is written in, by its place among them.
    Param(usize),
    /// The empty tuple, `()`, which a function with no return type gives.
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

    /// The enum `e` with these types, or unknown if one of them is.
    fn enumeration(e: usize, args: Vec<Ty>) -> Ty {
        if args.contains(&Ty::Unknown) {
            Ty::Unknown
        } else {
            Ty::Enum(e, args)
        }
    }

    /// The tuple of these types: the empty tuple if there are none, and
    /// unknown if one of them is.
    fn tuple(items: Vec<Ty>) -> Ty {
        if items.is_empty() {
            Ty::Nothing
        } else if items.contains(&Ty::Unknown) {
            Ty::Unknown
        } else {
            Ty::Tuple(items)
        }
    }

    /// The function with parameters and a result of these types, the
    /// result last, or unknown if one of them is.
    fn func(items: Vec<Ty>) -> Ty {
        if items.contains(&Ty::Unknown) {
            Ty::Unknown
        } else {
            Ty::Fn(items)
        }
    }

    /// The types in its brackets, for a struct or an enum, the types of its
    /// elements, for a tuple, and those of its parameters and its result,
    /// for a function.
    fn args(&self) -> &[Ty] {
        match self {
            Ty::Struct(_, args) | Ty::Enum(_, args) | Ty::Tuple(args) | Ty::Fn(args) => args,
            _ => &[],
        }
    }

    /// Whether it is a function, or holds one.
    fn holds_fn(&self) -> bool {
        matches!(self, Ty::Fn(_)) || self.args().iter().any(Ty::holds_fn)
    }

    /// The same kind of type, with these types in its brackets.
    fn with_args(&self, args: Vec<Ty>) -> Ty {
        match self {
            Ty::Struct(s, _) => Ty::structure(*s, args),
            Ty::Enum(e, _) => Ty::enumeration(*e, args),
            Ty::Tuple(_) => Ty::tuple(args),
            Ty::Fn(_) => Ty::func(args),
            t => t.clone(),
        }
    }

    /// The type with each type parameter replaced by the type at its place
    /// in `args`.
    fn subst(&self, args: &[Ty]) -> Ty {
        match self {
            Ty::Param(i) => args[*i].clone(),
            t => t.with_args(t.args().iter().map(|t| t.subst(args)).collect()),
        }
    }

    /// The same, where a parameter with no type in `args` is unknown.
    fn solved(&self, args: &[Option<Ty>]) -> Ty {
        match self {
            Ty::Param(i) => args[*i].clone().unwrap_or(Ty::Unknown),
            t => t.with_args(t.args().iter().map(|t| t.solved(args)).collect()),
        }
    }

    /// The same, where a parameter with no type in `args` is unknown, and
    /// a type that holds an unknown type keeps what is known of it, so that
    /// what is expected of a value can be partly known.
    fn partial(&self, args: &[Option<Ty>]) -> Ty {
        match self {
            Ty::Param(i) => args[*i].clone().unwrap_or(Ty::Unknown),
            Ty::Struct(s, of) => Ty::Struct(*s, of.iter().map(|t| t.partial(args)).collect()),
            Ty::Enum(e, of) => Ty::Enum(*e, of.iter().map(|t| t.partial(args)).collect()),
            Ty::Tuple(of) => Ty::Tuple(of.iter().map(|t| t.partial(args)).collect()),
            Ty::Fn(of) => Ty::Fn(of.iter().map(|t| t.partial(args)).collect()),
            t => t.clone(),
        }
    }

    /// Whether some part of it is not known.
    fn unknown(&self) -> bool {
        *self == Ty::Unknown || self.args().iter().any(Ty::unknown)
    }

    fn mentions(&self, p: usize) -> bool {
        match self {
            Ty::Param(i) => *i == p,
            t => t.args().iter().any(|t| t.mentions(p)),
        }
    }

    /// How many names it takes to write the type.
    fn size(&self) -> usize {
        1 + self.args().iter().map(Ty::size).sum::<usize>()
    }
}

/// Whether two types, either of which may be partly known, can be the
/// same.
fn agrees(a: &Ty, b: &Ty) -> bool {
    match (a, b) {
        (Ty::Unknown, _) | (_, Ty::Unknown) => true,
        (Ty::Struct(s, x), Ty::Struct(t, y)) | (Ty::Enum(s, x), Ty::Enum(t, y)) => {
            s == t && x.iter().zip(y).all(|(a, b)| agrees(a, b))
        }
        (Ty::Tuple(x), Ty::Tuple(y)) | (Ty::Fn(x), Ty::Fn(y)) => {
            x.len() == y.len() && x.iter().zip(y).all(|(a, b)| agrees(a, b))
        }
        (a, b) => a == b,
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
        (Ty::Struct(s, a), Ty::Struct(t, b)) | (Ty::Enum(s, a), Ty::Enum(t, b)) => {
            s == t && a.iter().zip(b).all(|(a, b)| unify(a, b, sol))
        }
        (Ty::Tuple(a), Ty::Tuple(b)) | (Ty::Fn(a), Ty::Fn(b)) => {
            a.len() == b.len() && a.iter().zip(b).all(|(a, b)| unify(a, b, sol))
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
            t => t.args().iter().any(|t| occurs(p, t, sol)),
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
        (Ty::Struct(s, x), Ty::Struct(t, y)) | (Ty::Enum(s, x), Ty::Enum(t, y)) => {
            s == t && x.iter().zip(&y).all(|(a, b)| overlap(a, b, sol))
        }
        (Ty::Tuple(x), Ty::Tuple(y)) | (Ty::Fn(x), Ty::Fn(y)) => {
            x.len() == y.len() && x.iter().zip(&y).all(|(a, b)| overlap(a, b, sol))
        }
        (Ty::Unknown, _) | (_, Ty::Unknown) => true,
        (a, b) => a == b,
    }
}

/// The type with each of its type parameters moved by `by` places.
fn shifted(t: &Ty, by: usize) -> Ty {
    match t {
        Ty::Param(i) => Ty::Param(i + by),
        t => t.with_args(t.args().iter().map(|t| shifted(t, by)).collect()),
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
    enum_names: HashMap<&'a str, usize>,
    trait_names: HashMap<&'a str, usize>,
    /// The functions of traits with each name, as the trait and the
    /// function's place in it.
    trait_fns: HashMap<&'a str, Vec<(usize, usize)>>,
    /// Whether each struct contains itself, directly or through others, as
    /// already reported. A type that names one is not known.
    cyclic: Vec<bool>,
    /// The same, for each enum.
    cyclic_enums: Vec<bool>,
    struct_scopes: Vec<Scope>,
    /// The type of each field of each struct, among its type parameters.
    fields: Vec<Vec<Ty>>,
    enum_scopes: Vec<Scope>,
    /// The type each case of each enum holds, if it holds one, among the
    /// enum's type parameters.
    cases: Vec<Vec<Option<Ty>>>,
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
        if name.is_empty() {
            let items = t.args.iter().map(|a| self.resolve(a, scope, errors));
            return if t.function {
                Ty::func(items.collect())
            } else {
                Ty::tuple(items.collect())
            };
        }
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
        if let Some(&e) = self.enum_names.get(name) {
            let args: Vec<Ty> = t
                .args
                .iter()
                .map(|a| self.resolve(a, scope, errors))
                .collect();
            let wanted = self.file.enums[e].generics.len();
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
            return if self.cyclic_enums[e] {
                Ty::Unknown
            } else {
                Ty::enumeration(e, args)
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
        } else if self.is_type(name) {
            Error::new(t.pos, format!("`{name}` is not a trait"))
        } else {
            Error::new(t.pos, format!("there is no trait named `{name}`"))
        };
        errors.push(error);
        Bound::Invalid
    }

    /// Whether the name is that of a type, other than a type parameter.
    fn is_type(&self, name: &str) -> bool {
        self.struct_names.contains_key(name)
            || self.enum_names.contains_key(name)
            || name == TEXT
            || TYPES_NOT_BUILT.contains(&name)
    }

    /// The name of a struct or an enum.
    fn type_name(&self, ty: &Ty) -> &'a str {
        match ty {
            Ty::Struct(s, _) => &self.file.structs[*s].name,
            Ty::Enum(e, _) => &self.file.enums[*e].name,
            _ => unreachable!("a struct or an enum"),
        }
    }

    /// The place of the case named `name` of enum `e`, if it has one.
    fn case_of(&self, e: usize, name: &str) -> Option<usize> {
        self.file.enums[e].cases.iter().position(|c| c.name == name)
    }

    /// The enum and its case, if the expression names a case in full, as
    /// in `Pet.none`, where `names` are the names defined.
    fn qualified_case(&self, of: &Expr, names: &[(String, Ty)]) -> Option<usize> {
        let Expr::Name { name, .. } = of else {
            return None;
        };
        if names.iter().any(|(n, _)| n == name) {
            return None;
        }
        self.enum_names.get(name.as_str()).copied()
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
            effects: {
                let mut built: Vec<String> = Vec::new();
                for (e, _) in &f.effects {
                    if BUILT.contains(&e.as_str()) && !built.contains(e) {
                        built.push(e.clone());
                    }
                }
                built
            },
            scope,
        }
    }

    /// How a type is written, with the type parameters of `scope`.
    fn text(&self, ty: &Ty, scope: &Scope) -> String {
        match ty {
            Ty::Text => TEXT.to_string(),
            Ty::Struct(_, args) | Ty::Enum(_, args) => {
                let name = self.type_name(ty);
                if args.is_empty() {
                    name.to_string()
                } else {
                    let args: Vec<String> = args.iter().map(|t| self.text(t, scope)).collect();
                    format!("{name}[{}]", args.join(", "))
                }
            }
            Ty::Tuple(items) => {
                let items: Vec<String> = items.iter().map(|t| self.text(t, scope)).collect();
                format!("({})", items.join(", "))
            }
            Ty::Fn(items) => {
                let items: Vec<String> = items.iter().map(|t| self.text(t, scope)).collect();
                let (gives, params) = items.split_last().expect("a result");
                format!("({}) -> {gives}", params.join(", "))
            }
            Ty::Param(p) => scope.names[*p].clone(),
            Ty::Nothing => "()".to_string(),
            Ty::Unknown => "nothing".to_string(),
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
                Ty::Tuple(items) => {
                    let items: Vec<String> = (items.iter())
                        .map(|a| text(defs, a, sol, vars, scope))
                        .collect();
                    format!("({})", items.join(", "))
                }
                Ty::Fn(items) => {
                    let items: Vec<String> = (items.iter())
                        .map(|a| text(defs, a, sol, vars, scope))
                        .collect();
                    let (gives, params) = items.split_last().expect("a result");
                    format!("({}) -> {gives}", params.join(", "))
                }
                Ty::Struct(_, args) | Ty::Enum(_, args) if !args.is_empty() => {
                    let args: Vec<String> = args
                        .iter()
                        .map(|a| text(defs, a, sol, vars, scope))
                        .collect();
                    format!("{}[{}]", defs.type_name(t), args.join(", "))
                }
                t => defs.text(t, scope),
            }
        }
        match pattern.solved(sol) {
            Ty::Text => "text".to_string(),
            Ty::Nothing => "nothing".to_string(),
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
    let args: Vec<String> = t.args.iter().map(written_type).collect();
    if t.function {
        let (gives, params) = args.split_last().expect("a result");
        format!("({}) -> {gives}", params.join(", "))
    } else if t.name.is_empty() {
        format!("({})", args.join(", "))
    } else if args.is_empty() {
        t.name.clone()
    } else {
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
        Expr::Interp { parts, .. } => {
            let mut out = String::from("\"");
            for part in parts {
                match part {
                    Part::Text(text) => {
                        let quoted = written_expr(&Expr::Text {
                            text: text.clone(),
                            pos: Pos::START,
                        });
                        out.push_str(&quoted[1..quoted.len() - 1]);
                    }
                    Part::Expr(e) => {
                        let Expr::Call(call) = e else {
                            unreachable!("the call of `Fmt.fmt`")
                        };
                        out.push_str(&format!("{{{}}}", written_expr(&call.args[0].value)));
                    }
                }
            }
            out.push('"');
            out
        }
        Expr::Name { name, .. } => name.clone(),
        Expr::Call(call) => {
            let args: Vec<String> = (call.args.iter())
                .filter(|a| !a.after)
                .map(|a| match &a.name {
                    Some((name, _)) => format!("{name}: {}", written_expr(&a.value)),
                    None => written_expr(&a.value),
                })
                .collect();
            let mut text = match &call.qualifier {
                Some((q, _)) => format!("{q}.{}({})", call.callee, args.join(", ")),
                None => format!("{}({})", call.callee, args.join(", ")),
            };
            for a in call.args.iter().filter(|a| a.after) {
                text.push_str(&format!(" {}", written_expr(&a.value)));
            }
            text
        }
        Expr::Field { of, name, .. } => format!("{}.{name}", written_expr(of)),
        Expr::Case { name, payload, .. } => match payload {
            Some(p) => format!(".{name}({})", written_expr(p)),
            None => format!(".{name}"),
        },
        Expr::Match(m) => format!("match {} {{ ... }}", written_expr(&m.of)),
        Expr::Tuple { items, .. } => {
            let items: Vec<String> = items.iter().map(written_expr).collect();
            format!("({})", items.join(", "))
        }
        Expr::Lambda(l) if l.params.is_empty() => "{ ... }".to_string(),
        Expr::Lambda(l) => {
            let params: Vec<&str> = l.params.iter().map(|(p, _)| p.as_str()).collect();
            format!("fn({}) {{ ... }}", params.join(", "))
        }
    }
}

/// A definition of the file, by its place among those of its kind.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Item {
    Fn(usize),
    Struct(usize),
    Enum(usize),
    Trait(usize),
    Impl(usize),
}

/// The kinds of definition that claim a name.
#[derive(Clone, Copy, PartialEq, Eq)]
enum Claim {
    Fn(usize),
    Struct(usize),
    Enum(usize),
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
    let file = with_prelude(file);
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
    defined.extend(
        (file.enums.iter().enumerate()).map(|(i, e)| (e.pos, e.name.as_str(), Claim::Enum(i))),
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
    let mut enum_names: HashMap<&str, usize> = HashMap::new();
    let mut trait_names: HashMap<&str, usize> = HashMap::new();
    let mut trait_fns: HashMap<&str, Vec<(usize, usize)>> = HashMap::new();
    for (pos, name, claim) in defined {
        let is_type = matches!(claim, Claim::Struct(_) | Claim::Enum(_) | Claim::Trait(_));
        let prelude = |what: &str| {
            Error::unsupported(
                pos,
                &format!("defining `{what}`, which the prelude defines,"),
            )
        };
        let prelude_name = match claim {
            Claim::Fn(_) | Claim::TraitFn(..) => name == "print",
            Claim::Struct(_) => name == TEXT,
            Claim::Enum(_) => name == TEXT || name == "Option" || name == "Result",
            Claim::Trait(_) => {
                name == TEXT || TRAITS_NOT_BUILT.contains(&name) || PRELUDE_TRAITS.contains(&name)
            }
        };
        if prelude_name && !of_prelude(pos) {
            errors.push(prelude(name));
        } else if is_type && struct_names.contains_key(name) {
            errors.push(Error::new(
                pos,
                format!("there is already a struct named `{name}`"),
            ));
        } else if is_type && enum_names.contains_key(name) {
            errors.push(Error::new(
                pos,
                format!("there is already an enum named `{name}`"),
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
                Claim::Enum(i) => {
                    enum_names.insert(name, i);
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
            } else if let Some(&e) = enum_names.get(name) {
                found.push(Item::Enum(e));
            } else if let Some(&k) = trait_names.get(name) {
                found.push(Item::Trait(k));
            }
        };
        // A case written in full names its enum, and a function may be
        // passed as a value by its name.
        let called = |values: Vec<&Expr>, found: &mut Vec<Item>| {
            for e in exprs(values.iter().copied()) {
                if let Expr::Field { of, .. } = e
                    && let Expr::Name { name, .. } = &**of
                {
                    type_name(name, found);
                }
                if let Expr::Name { name, .. } = e
                    && let Some(&f) = function_names.get(name.as_str())
                {
                    found.push(Item::Fn(f));
                }
            }
            for call in calls(values) {
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
            called(values(&f.body).chain(defaults).collect(), found);
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
                called(defaults.collect(), &mut found);
                found.extend(impls_for(&file, &s.name));
            }
            Item::Enum(e) => {
                let e = &file.enums[e];
                for t in e.cases.iter().filter_map(|c| c.ty.as_ref()) {
                    for (name, _) in type_names(t) {
                        type_name(name, &mut found);
                    }
                }
                for name in bounds_named(&e.generics) {
                    type_name(name, &mut found);
                }
                found.extend(impls_for(&file, &e.name));
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
    let reached_types: Vec<Def> = (reached.iter())
        .filter_map(|r| match r {
            Item::Struct(s) => Some(Def::Struct(*s)),
            Item::Enum(e) => Some(Def::Enum(*e)),
            _ => None,
        })
        .collect();

    let mut defs = Defs {
        file: &file,
        function_names,
        struct_names,
        enum_names,
        trait_names,
        trait_fns,
        cyclic: vec![false; file.structs.len()],
        cyclic_enums: vec![false; file.enums.len()],
        struct_scopes: Vec::new(),
        fields: Vec::new(),
        enum_scopes: Vec::new(),
        cases: Vec::new(),
        signatures: Vec::new(),
        trait_sigs: Vec::new(),
        outs: (file.traits.iter())
            .map(|t| t.generics.iter().map(|g| g.out.is_some()).collect())
            .collect(),
        impls: Vec::new(),
        impls_of: vec![Vec::new(); file.traits.len()],
    };
    let mut done = Vec::new();
    for &d in &reached_types {
        if !done.contains(&d) {
            cycles(&mut defs, d, &mut Vec::new(), &mut done, &mut errors);
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
    defs.enum_scopes = file.enums.iter().map(|e| defs.scope(&e.generics)).collect();
    defs.cases = (file.enums.iter().enumerate())
        .map(|(i, e)| {
            (e.cases.iter())
                .map(|c| c.ty.as_ref().map(|t| defs.quietly(t, &defs.enum_scopes[i])))
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
        types: HashMap::new(),
        value_calls: HashSet::new(),
        fn_values: HashMap::new(),
    };
    checker.main(&file.functions[main]);
    for &item in &reached {
        match item {
            Item::Fn(f) => checker.signature(f),
            Item::Struct(s) => checker.structure(s),
            Item::Enum(e) => checker.enumeration(e),
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
            Item::Struct(_) | Item::Enum(_) | Item::Trait(_) => {}
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
    let typed = Typed {
        defs: &defs,
        types: checker.types,
        value_calls: checker.value_calls,
        fn_values: checker.fn_values,
    };
    let mut errors = Vec::new();
    typed.copies(&start, &mut Vec::new(), &mut Vec::new(), &mut errors);
    if !errors.is_empty() {
        return Err(errors);
    }

    let mut lowering = Lowering {
        typed: &typed,
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
        if !reached.contains(&item) && !of_prelude(pos) {
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
    for (i, e) in file.enums.iter().enumerate() {
        dead(Item::Enum(i), e.pos, format!("the enum `{}`", e.name));
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
    for stmt in statements(&f.body) {
        let defined = match stmt {
            Stmt::Define { name, pos, .. } => vec![(name.as_str(), *pos)],
            Stmt::Destructure { pattern, .. } => pattern_names(pattern),
            Stmt::Assign { .. } | Stmt::Expr(_) => Vec::new(),
        };
        for (name, pos) in defined {
            if !read.contains(&name) {
                out.push(
                    Error::new(
                        pos,
                        format!("`{name}` is defined in `{}` and never read", f.name),
                    )
                    .with_fix(format!("delete the binding, or read `{name}`")),
                );
            }
        }
    }
}

/// Each bare name in a pattern, with where it is, from left to right.
fn pattern_names(pattern: &Pattern) -> Vec<(&str, Pos)> {
    match pattern {
        Pattern::Bind { name, pos, .. } => vec![(name.as_str(), *pos)],
        Pattern::Case { inner, .. } => inner.as_deref().map_or(Vec::new(), pattern_names),
        Pattern::Tuple { items, .. } => items.iter().flat_map(pattern_names).collect(),
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

/// A struct or an enum, by its place among those of its kind.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Def {
    Struct(usize),
    Enum(usize),
}

/// The implementations whose types name `name`, which live with that type.
fn impls_for(file: &File, name: &str) -> Vec<Item> {
    (file.impls.iter().enumerate())
        .filter(|(_, im)| im.args.iter().flat_map(type_names).any(|(n, _)| n == name))
        .map(|(i, _)| Item::Impl(i))
        .collect()
}

/// Reports each type, of a field or of a case, that closes a cycle of
/// structs and enums that contain one another, in a walk from `d`, and
/// marks those on the cycle. `path` is the definitions being walked, and
/// `done` those already walked.
fn cycles(
    defs: &mut Defs,
    d: Def,
    path: &mut Vec<Def>,
    done: &mut Vec<Def>,
    errors: &mut Vec<Error>,
) {
    path.push(d);
    let file = defs.file;
    let types: Vec<&Type> = match d {
        Def::Struct(s) => file.structs[s].fields.iter().map(|f| &f.ty).collect(),
        Def::Enum(e) => file.enums[e]
            .cases
            .iter()
            .filter_map(|c| c.ty.as_ref())
            .collect(),
    };
    for ty in types {
        for (name, pos) in type_names(ty) {
            let t = if let Some(&s) = defs.struct_names.get(name) {
                Def::Struct(s)
            } else if let Some(&e) = defs.enum_names.get(name) {
                Def::Enum(e)
            } else {
                continue;
            };
            if let Some(at) = path.iter().position(|p| *p == t) {
                let what = match t {
                    Def::Struct(_) => "a struct that contains itself, directly or through others,",
                    Def::Enum(_) => "an enum that contains itself, directly or through others,",
                };
                errors.push(Error::unsupported(pos, what));
                for &p in &path[at..] {
                    match p {
                        Def::Struct(s) => defs.cyclic[s] = true,
                        Def::Enum(e) => defs.cyclic_enums[e] = true,
                    }
                }
            } else if !done.contains(&t) {
                cycles(defs, t, path, done, errors);
            }
        }
    }
    path.pop();
    done.push(d);
}

/// Every name in a type, with where it is: its own, and those of the
/// types in its brackets.
fn type_names(t: &Type) -> Vec<(&str, Pos)> {
    let mut out = Vec::new();
    if !t.name.is_empty() {
        out.push((t.name.as_str(), t.pos));
    }
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
        Stmt::Define { value, .. }
        | Stmt::Assign { value, .. }
        | Stmt::Destructure { value, .. }
        | Stmt::Expr(value) => value,
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
        Expr::Case { payload, .. } => {
            if let Some(p) = payload {
                walk(p, out);
            }
        }
        Expr::Match(m) => {
            walk(&m.of, out);
            for arm in &m.arms {
                for value in values(&arm.body) {
                    walk(value, out);
                }
            }
        }
        Expr::Tuple { items, .. } => {
            for item in items {
                walk(item, out);
            }
        }
        Expr::Lambda(l) => {
            for value in values(&l.body) {
                walk(value, out);
            }
        }
        Expr::Interp { parts, .. } => {
            for part in parts {
                if let Part::Expr(e) = part {
                    walk(e, out);
                }
            }
        }
        Expr::Text { .. } | Expr::Name { .. } => {}
    }
}

/// Every statement of a body, and those of the blocks of each `match` and
/// each lambda in them, each before those inside it.
fn statements(body: &[Stmt]) -> Vec<&Stmt> {
    /// The blocks of each `match` and each lambda in the expression that is
    /// not inside another's block.
    fn blocks<'a>(e: &'a Expr, out: &mut Vec<&'a [Stmt]>) {
        match e {
            Expr::Call(call) => {
                for arg in &call.args {
                    blocks(&arg.value, out);
                }
            }
            Expr::Field { of, .. } => blocks(of, out),
            Expr::Case {
                payload: Some(p), ..
            } => blocks(p, out),
            Expr::Match(m) => {
                blocks(&m.of, out);
                out.extend(m.arms.iter().map(|arm| arm.body.as_slice()));
            }
            Expr::Tuple { items, .. } => {
                for item in items {
                    blocks(item, out);
                }
            }
            Expr::Lambda(l) => out.push(&l.body),
            Expr::Interp { parts, .. } => {
                for part in parts {
                    if let Part::Expr(e) = part {
                        blocks(e, out);
                    }
                }
            }
            Expr::Case { .. } | Expr::Text { .. } | Expr::Name { .. } => {}
        }
    }
    let mut out = Vec::new();
    for (stmt, value) in body.iter().zip(values(body)) {
        out.push(stmt);
        let mut found = Vec::new();
        blocks(value, &mut found);
        for block in found {
            out.extend(statements(block));
        }
    }
    out
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
    for stmt in statements(&f.body) {
        if let Stmt::Define { ty: Some(t), .. } | Stmt::Destructure { ty: Some(t), .. } = stmt {
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
    /// A case of an enum written in full, by its place among the enum's
    /// cases.
    Case(usize),
    /// A copy of a function to run, and what it gives.
    Run(Instance, Ty),
    /// The function a name holds, which a parameter or a lambda's parameter
    /// was given.
    Value,
}

/// For each argument of a checked call, the place of its parameter.
fn params_of(params: &[(String, Ty)], args: &[Arg]) -> Vec<usize> {
    let mut next = 0;
    (args.iter())
        .map(|arg| match &arg.name {
            Some((name, _)) => params.iter().position(|(p, _)| p == name).expect("checked"),
            None if arg.after => params.len() - 1,
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
                None if arg.after && by_position => {
                    if let Some(g) = given.last_mut() {
                        *g = true;
                    }
                }
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
}

/// The address of a node of the syntax tree, by which its type is recorded.
fn addr<T>(node: &T) -> usize {
    node as *const T as usize
}

/// The definitions, with the type of each expression checked, among the
/// type parameters of the definition it is written in.
struct Typed<'a> {
    defs: &'a Defs<'a>,
    types: HashMap<usize, Ty>,
    /// The calls of a name that holds a function, by their addresses.
    value_calls: HashSet<usize>,
    /// The names of functions passed as values, by their addresses, with
    /// the function each names.
    fn_values: HashMap<usize, usize>,
}

impl<'a> Typed<'a> {
    /// The type of a checked expression, with the type parameters of the
    /// definition it is written in replaced by `env`.
    fn type_of(&self, e: &Expr, env: &[Ty]) -> Ty {
        self.types[&addr(e)].subst(env)
    }

    /// What a checked call does, with the type parameters of the definition
    /// it is written in replaced by `env`.
    fn callee(&self, call: &Call, env: &[Ty]) -> Callee {
        let defs = self.defs;
        let given = |i: usize| self.type_of(&call.args[i].value, env);
        let solve = |params: &[(String, Ty)], vars: usize| {
            let mut sol = vec![None; vars];
            for (i, p) in params_of(params, &call.args).into_iter().enumerate() {
                unify(&params[p].1, &given(i), &mut sol);
            }
            sol.into_iter()
                .map(|t| t.unwrap_or(Ty::Unknown))
                .collect::<Vec<Ty>>()
        };
        if self.value_calls.contains(&addr(call)) {
            return Callee::Value;
        }
        let (k, fi) = match &call.qualifier {
            None => {
                let name = call.callee.as_str();
                if defs.struct_names.contains_key(name) {
                    let Ty::Struct(s, args) = self.types[&addr(call)].subst(env) else {
                        unreachable!("checked")
                    };
                    return Callee::Construct(s, args);
                }
                if name == "print" {
                    return Callee::Print;
                }
                if let Some(&f) = defs.function_names.get(name) {
                    let sig = &defs.signatures[f];
                    let args = solve(&sig.params, sig.scope.names.len());
                    let gives = sig.gives.subst(&args);
                    let template = Template::Free(f);
                    return Callee::Run(Instance { template, args }, gives);
                }
                defs.trait_fns[name][0]
            }
            Some((q, _)) => {
                if let Some(&e) = defs.enum_names.get(q.as_str()) {
                    return Callee::Case(defs.case_of(e, &call.callee).expect("checked"));
                }
                let k = defs.trait_names[q.as_str()];
                let at = defs.file.traits[k]
                    .functions
                    .iter()
                    .position(|f| f.name == call.callee);
                (k, at.expect("checked"))
            }
        };
        let sig = &defs.trait_sigs[k][fi];
        let types = solve(&sig.params, sig.scope.names.len());
        let Found::Yes(all, Some((i, args))) =
            defs.find(k, &types, &Scope::default(), &mut Vec::new())
        else {
            unreachable!("checked")
        };
        let template = Template::Impl(i, fi);
        Callee::Run(Instance { template, args }, sig.gives.subst(&all))
    }

    /// The types that stand for the type parameters of the defaults a call
    /// leaves out: those of the function, of the trait, or of the struct
    /// they are written in.
    fn env_of(&self, callee: &Callee) -> Vec<Ty> {
        match callee {
            Callee::Run(instance, _) => match instance.template {
                Template::Free(_) => instance.args.clone(),
                Template::Impl(i, _) => {
                    let info = self.defs.impls[i].as_ref().expect("known");
                    info.args.iter().map(|t| t.subst(&instance.args)).collect()
                }
            },
            Callee::Construct(_, args) => args.clone(),
            Callee::Print | Callee::Case(_) | Callee::Value => Vec::new(),
        }
    }

    /// The calls in the defaults a call leaves out, which run where the
    /// call is written, each with the types that stand for the type
    /// parameters of the definition the default is in, and each followed
    /// by the calls in the defaults it leaves out in turn. A default
    /// already in `stack` is not filled in again, and the place of the call
    /// that leaves it out goes in `cycles`.
    fn filled(
        &self,
        call: &'a Call,
        env: &[Ty],
        stack: &mut Vec<&'a Expr>,
        out: &mut Vec<(&'a Call, Vec<Ty>)>,
        cycles: &mut Vec<Pos>,
    ) {
        let left_out = self.defs.left_out(call);
        if left_out.is_empty() || self.value_calls.contains(&addr(call)) {
            return;
        }
        let inner_env = self.env_of(&self.callee(call, env));
        for (_, default) in left_out {
            if stack.iter().any(|d| std::ptr::eq(*d, default)) {
                cycles.push(call.pos);
                continue;
            }
            stack.push(default);
            for inner in calls([default]) {
                out.push((inner, inner_env.clone()));
                self.filled(inner, &inner_env, stack, out, cycles);
            }
            stack.pop();
        }
    }

    /// The calls in a copy's body that run a function, outermost first and
    /// then from left to right, each with where it is and the copy it runs.
    /// The calls in a default a call leaves out follow that call. Each call
    /// that leaves out the default it is written in is in the second list.
    fn calls_of(&self, instance: &Instance) -> (Vec<(Pos, Instance)>, Vec<Pos>) {
        let view = self.defs.view(instance.template);
        let mut out = Vec::new();
        let mut cycles = Vec::new();
        for e in exprs(values(&view.f.body)) {
            if let Some(&f) = self.fn_values.get(&addr(e)) {
                let template = Template::Free(f);
                let args = Vec::new();
                out.push((e.pos(), Instance { template, args }));
            }
            let Expr::Call(call) = e else {
                continue;
            };
            let mut all = vec![(call, instance.args.clone())];
            self.filled(call, &instance.args, &mut Vec::new(), &mut all, &mut cycles);
            for (call, env) in all {
                if let Callee::Run(callee, _) = self.callee(call, &env) {
                    out.push((call.pos, callee));
                }
            }
        }
        (out, cycles)
    }

    /// Reports each call that would need a copy of a function without end:
    /// one already being walked, or one of a function being walked for
    /// larger types, which would grow forever. `path` is the copies being
    /// walked from `main`, and `done` those already walked.
    fn copies(
        &self,
        instance: &Instance,
        path: &mut Vec<Instance>,
        done: &mut Vec<Instance>,
        errors: &mut Vec<Error>,
    ) {
        path.push(instance.clone());
        let (calls, cycles) = self.calls_of(instance);
        errors.extend(cycles.into_iter().map(default_cycle));
        for (pos, callee) in calls {
            let size = |i: &Instance| i.args.iter().map(Ty::size).sum::<usize>();
            let endless = path.iter().any(|p| {
                p.template == callee.template && (*p == callee || size(&callee) > size(p))
            });
            if endless {
                errors.push(Error::unsupported(
                    pos,
                    "a function that calls itself, directly or through others,",
                ));
            } else if !done.contains(&callee) {
                self.copies(&callee, path, done, errors);
            }
        }
        path.pop();
        done.push(instance.clone());
    }
}

/// What kind of definition a list of type parameters belongs to.
#[derive(Clone, Copy, PartialEq, Eq)]
enum Owner {
    Function,
    Struct,
    Enum,
    Trait,
    Impl,
}

struct Checker<'a> {
    defs: &'a Defs<'a>,
    errors: Vec<Error>,
    /// The type of each expression checked, by its address.
    types: HashMap<usize, Ty>,
    /// The calls of a name that holds a function, by their addresses.
    value_calls: HashSet<usize>,
    /// The names of functions passed as values, by their addresses, with
    /// the function each names.
    fn_values: HashMap<usize, usize>,
}

/// What is expected of an expression's value.
#[derive(Clone, Debug)]
enum Want {
    /// It is a statement, and its value is not used.
    Unused,
    /// Its value is used, and nothing says what type it must be.
    Any,
    /// Its value must be of this type, which may be partly known.
    Ty(Ty),
}

/// How a block ends.
enum End<'e> {
    Empty,
    /// With a binding or an assignment, at this place.
    Binding(Pos),
    /// With an expression, which gives this type.
    Value(Ty, &'e Expr),
}

/// A pattern once checked: `Any` matches every value, as a bare name does,
/// `Case` matches one case of an enum, by its place, and what it holds if a
/// pattern for it is given, and `Tuple` matches a tuple whose elements its
/// patterns match.
enum Pat {
    Any,
    Case(usize, Option<Box<Pat>>),
    Tuple(Vec<Pat>),
}

const ANY: Pat = Pat::Any;

/// Rows of patterns, one column for each value matched at once.
type Rows<'p> = Vec<Vec<&'p Pat>>;

/// The rows whose first pattern matches case `k`, with that pattern
/// replaced by the one for what the case holds, if it `holds` something. A
/// pattern that matches anything matches what it holds with anything too.
fn specialize<'p>(rows: &[Vec<&'p Pat>], k: usize, holds: bool) -> Rows<'p> {
    (rows.iter())
        .filter_map(|r| {
            let inner = match r[0] {
                Pat::Any => &ANY,
                Pat::Case(j, inner) if *j == k => inner.as_deref().unwrap_or(&ANY),
                Pat::Case(..) | Pat::Tuple(_) => return None,
            };
            let mut row = if holds { vec![inner] } else { Vec::new() };
            row.extend(&r[1..]);
            Some(row)
        })
        .collect()
}

/// The rows with their first pattern, which matches a tuple of `n`,
/// replaced by a pattern for each of its elements.
fn spread<'p>(rows: &[Vec<&'p Pat>], n: usize) -> Rows<'p> {
    (rows.iter())
        .map(|r| {
            let mut row: Vec<&Pat> = match r[0] {
                Pat::Tuple(items) => items.iter().collect(),
                _ => vec![&ANY; n],
            };
            row.extend(&r[1..]);
            row
        })
        .collect()
}

/// Whether some row's first pattern names a case.
fn names_a_case(rows: &[Vec<&Pat>]) -> bool {
    rows.iter().any(|r| matches!(r[0], Pat::Case(..)))
}

/// The values of types `tys` that none of the rows matches, as the
/// patterns that match them are written, one for each type, in the order
/// the enums declare their cases. A value no row matches any part of is a
/// bare name, and a case none of the rows names is one pattern, with a bare
/// name for what it holds.
fn missing_rows(defs: &Defs, rows: &[Vec<&Pat>], tys: &[Ty]) -> Vec<Vec<String>> {
    let Some((ty, rest)) = tys.split_first() else {
        return if rows.is_empty() {
            vec![Vec::new()]
        } else {
            Vec::new()
        };
    };
    let any = |n: usize| vec!["_".to_string(); n];
    match ty {
        Ty::Tuple(_) if rows.is_empty() => vec![any(tys.len())],
        Ty::Tuple(items) => {
            let tys: Vec<Ty> = items.iter().chain(rest).cloned().collect();
            (missing_rows(defs, &spread(rows, items.len()), &tys).into_iter())
                .map(|left| {
                    let (first, after) = left.split_at(items.len());
                    let mut out = vec![format!("({})", first.join(", "))];
                    out.extend_from_slice(after);
                    out
                })
                .collect()
        }
        Ty::Enum(e, args) if rows.is_empty() || names_a_case(rows) => {
            let mut out = Vec::new();
            for (k, case) in defs.file.enums[*e].cases.iter().enumerate() {
                let holds = defs.cases[*e][k].as_ref().map(|t| t.subst(args));
                let tys: Vec<Ty> = holds.iter().chain(rest).cloned().collect();
                let inside = specialize(rows, k, holds.is_some());
                let left = if inside.is_empty() {
                    vec![any(tys.len())]
                } else {
                    missing_rows(defs, &inside, &tys)
                };
                for left in left {
                    let (first, after) = match holds {
                        Some(_) => (format!(".{}({})", case.name, left[0]), &left[1..]),
                        None => (format!(".{}", case.name), &left[..]),
                    };
                    let mut row = vec![first];
                    row.extend_from_slice(after);
                    out.push(row);
                }
            }
            out
        }
        _ => {
            let rows: Rows = rows.iter().map(|r| r[1..].to_vec()).collect();
            (missing_rows(defs, &rows, rest).into_iter())
                .map(|left| {
                    let mut row = vec!["_".to_string()];
                    row.extend(left);
                    row
                })
                .collect()
        }
    }
}

/// The values of type `ty` that none of the patterns matches, as the
/// patterns that match them are written.
fn missing(defs: &Defs, patterns: &[&Pat], ty: &Ty) -> Vec<String> {
    let rows: Rows = patterns.iter().map(|p| vec![*p]).collect();
    (missing_rows(defs, &rows, std::slice::from_ref(ty)).into_iter())
        .map(|mut left| left.remove(0))
        .collect()
}

/// Whether `row` matches some values of types `tys` that none of `rows`
/// matches.
fn useful_row(defs: &Defs, rows: &[Vec<&Pat>], row: &[&Pat], tys: &[Ty]) -> bool {
    let Some((ty, rest)) = tys.split_first() else {
        return rows.is_empty();
    };
    match (row[0], ty) {
        (_, Ty::Tuple(items)) => {
            let tys: Vec<Ty> = items.iter().chain(rest).cloned().collect();
            let rows = spread(rows, items.len());
            let row = spread(&[row.to_vec()], items.len()).remove(0);
            useful_row(defs, &rows, &row, &tys)
        }
        (Pat::Case(k, _), Ty::Enum(e, args)) => {
            let holds = defs.cases[*e][*k].as_ref().map(|t| t.subst(args));
            let tys: Vec<Ty> = holds.iter().chain(rest).cloned().collect();
            let rows = specialize(rows, *k, holds.is_some());
            let row = specialize(&[row.to_vec()], *k, holds.is_some()).remove(0);
            useful_row(defs, &rows, &row, &tys)
        }
        (Pat::Any, Ty::Enum(e, args)) if names_a_case(rows) => (0..defs.file.enums[*e].cases.len())
            .any(|k| {
                let holds = defs.cases[*e][k].as_ref().map(|t| t.subst(args));
                let tys: Vec<Ty> = holds.iter().chain(rest).cloned().collect();
                let rows = specialize(rows, k, holds.is_some());
                let row = specialize(&[row.to_vec()], k, holds.is_some()).remove(0);
                useful_row(defs, &rows, &row, &tys)
            }),
        _ => {
            let rows: Rows = rows.iter().map(|r| r[1..].to_vec()).collect();
            useful_row(defs, &rows, &row[1..], rest)
        }
    }
}

/// Whether the pattern matches some value of type `ty` that none of the
/// patterns before it matches.
fn useful(defs: &Defs, before: &[&Pat], pattern: &Pat, ty: &Ty) -> bool {
    let rows: Rows = before.iter().map(|p| vec![*p]).collect();
    useful_row(defs, &rows, &[pattern], std::slice::from_ref(ty))
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
    /// Defines the names a pattern binds, each with its type and whether it
    /// is a `var`.
    fn define(&mut self, binds: Vec<(String, Ty, bool)>) {
        for (name, ty, var) in binds {
            if var {
                self.vars.push(name.clone());
            }
            self.names.push((name, ty));
        }
    }

    /// The function whose body this is.
    fn owner(&self) -> &'a Function {
        self.f.expect("a function's body")
    }

    /// Where the body is, as an error names it after a place.
    fn in_owner(&self) -> String {
        self.f
            .map_or(String::new(), |f| format!(" in `{}`", f.name))
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
    /// type parameters of `scope`, and holds no function type, and gives
    /// the type, which is unknown if it holds a mistake.
    fn ty(&mut self, t: &Type, scope: &Scope) -> Ty {
        let ty = self.defs.resolve(t, scope, &mut self.errors);
        if self.no_function(t) { ty } else { Ty::Unknown }
    }

    /// Reports the first function type in a type, which only a parameter's
    /// type may be.
    fn no_function(&mut self, t: &Type) -> bool {
        if t.function {
            self.errors.push(Error::unsupported(
                t.pos,
                "a function type other than a parameter's type",
            ));
            return false;
        }
        t.args.iter().all(|a| self.no_function(a))
    }

    /// Checks the type of a parameter, which may be a function type whose
    /// own parameters' types may be function types in turn.
    fn param_ty(&mut self, t: &Type, scope: &Scope) {
        self.defs.resolve(t, scope, &mut self.errors);
        self.passed(t);
    }

    fn passed(&mut self, t: &Type) {
        match t.args.split_last() {
            Some((gives, params)) if t.function => {
                for p in params {
                    self.passed(p);
                }
                self.no_function(gives);
            }
            _ => {
                self.no_function(t);
            }
        }
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
                || defs.enum_names.contains_key(name)
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
                (Some(b), Owner::Enum) => self.errors.push(Error::unsupported(
                    b.pos,
                    "a bound on a type parameter of an enum",
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

    /// Checks an enum: its type parameters, each named by a case's type,
    /// and its cases, each named once, each of a type there is.
    fn enumeration(&mut self, e: usize) {
        let defs = self.defs;
        let def = &defs.file.enums[e];
        let scope = &defs.enum_scopes[e];
        self.generics(&def.generics, &format!("`{}`", def.name), Owner::Enum);
        for g in &def.generics {
            let named = (def.cases.iter())
                .filter_map(|c| c.ty.as_ref())
                .any(|t| type_names(t).iter().any(|(n, _)| *n == g.name));
            if !named {
                self.errors.push(Error::unsupported(
                    g.pos,
                    "a type parameter that no case's type names",
                ));
            }
        }
        for (i, case) in def.cases.iter().enumerate() {
            if def.cases[..i].iter().any(|c| c.name == case.name) {
                self.errors.push(Error::new(
                    case.pos,
                    format!("`{}` is already a case of `{}`", case.name, def.name),
                ));
            }
            if let Some(t) = &case.ty {
                self.ty(t, scope);
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
        let gives = self.expr(&mut body, value, &Want::Ty(needed.clone()));
        self.expect(&body, value, &gives, needed);
    }

    /// Checks the defaults of the parameters of `f`, whose signature is
    /// `sig`: the parameters with one come after those without, and each
    /// default is as `default` requires.
    fn defaults(&mut self, f: &'a Function, sig: &'a Signature) {
        let names: Vec<&str> = f.params.iter().map(|p| p.name.as_str()).collect();
        let mut first = None;
        for (i, (p, (_, ty))) in f.params.iter().zip(&sig.params).enumerate() {
            // A final parameter that takes a function may come after those
            // with defaults, as a lambda after a call is for it.
            let last_fn = i + 1 == f.params.len() && p.ty.function;
            match (&p.default, first) {
                (None, Some(_)) if last_fn => {}
                (Some(_), _) if p.ty.function => {}
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
            if BUILT.contains(&effect) {
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
            self.param_ty(&p.ty, scope);
            if let Some(value) = &p.default
                && p.ty.function
            {
                self.errors.push(Error::unsupported(
                    value.pos(),
                    "a default for a parameter that takes a function",
                ));
            }
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
            } else if defs.is_type(name) {
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
            (_, Ty::Unknown) | (Ty::Unknown, _) => {}
            (Ty::Nothing, needed) if needed != given => {
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
            (_, Ty::Unknown) | (Ty::Unknown, _) => {}
            (Ty::Nothing, pattern) if !matches!(pattern, Ty::Nothing | Ty::Param(_)) => {
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
                unknown_effect |= !BUILT.contains(&effect)
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
        let want = match view.result {
            Some(_) => Want::Ty(view.gives.clone()),
            None => Want::Unused,
        };
        match (self.block(&mut body, &f.body, &want), &view.result) {
            (End::Empty | End::Binding(_), Some(_)) if view.gives == Ty::Nothing => {}
            (End::Empty, Some(t)) => self.errors.push(Error::new(
                f.end,
                format!(
                    "`{}` returns `{t}`, and its body is empty, which gives nothing",
                    f.name
                ),
            )),
            (End::Binding(pos), Some(t)) => self.ends_with_nothing(f, pos, t),
            (End::Value(gives, value), Some(_)) => self.expect(&body, value, &gives, &view.gives),
            (
                End::Value(
                    gives
                    @ (Ty::Text | Ty::Struct(..) | Ty::Enum(..) | Ty::Tuple(_) | Ty::Param(_)),
                    value,
                ),
                None,
            ) => self.errors.push(Error::new(
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

    /// The error for defining `name` at `pos` where it is already defined.
    fn defined_twice(&self, body: &Body, name: &str, pos: Pos) -> Error {
        match body.f {
            Some(f) => Error::new(pos, format!("`{name}` is already defined in `{}`", f.name)),
            None => Error::new(pos, format!("`{name}` is already defined here")),
        }
    }

    /// Checks the statements of a block in order, the last against `last`,
    /// and gives how it ends.
    fn block<'e>(&mut self, body: &mut Body<'a>, stmts: &'e [Stmt], last: &Want) -> End<'e> {
        let mut end = End::Empty;
        for (i, stmt) in stmts.iter().enumerate() {
            let is_last = i + 1 == stmts.len();
            end = match stmt {
                Stmt::Define {
                    name, pos, value, ..
                } if matches!(value, Expr::Lambda(_)) => {
                    self.errors.push(Error::new(
                        value.pos(),
                        "a lambda cannot be bound to a name, as it is only passed to a call",
                    ));
                    if !body.names.iter().any(|(n, _)| n == name) {
                        body.names.push((name.clone(), Ty::Unknown));
                    }
                    End::Binding(*pos)
                }
                Stmt::Assign { pos, value, .. } if matches!(value, Expr::Lambda(_)) => {
                    self.errors.push(Error::new(
                        value.pos(),
                        "a lambda cannot be bound to a name, as it is only passed to a call",
                    ));
                    End::Binding(*pos)
                }
                Stmt::Destructure { pattern, value, .. } if matches!(value, Expr::Lambda(_)) => {
                    self.errors.push(Error::new(
                        value.pos(),
                        "a lambda cannot be bound to a name, as it is only passed to a call",
                    ));
                    let mut binds = Vec::new();
                    self.pattern(
                        body,
                        pattern,
                        &Ty::Unknown,
                        &mut binds,
                        Some(&mut Vec::new()),
                    );
                    body.define(binds);
                    End::Binding(pattern.pos())
                }
                Stmt::Expr(value @ Expr::Lambda(l)) => {
                    let message = if is_last && matches!(last, Want::Ty(_)) {
                        "a lambda cannot be returned, as it is only passed to a call"
                    } else if l.params.is_empty() {
                        "a block that stands alone is not an expression"
                    } else {
                        "a lambda that stands alone is not an expression, as it is only passed to \
                         a call"
                    };
                    self.errors.push(Error::new(l.pos, message));
                    End::Value(Ty::Unknown, value)
                }
                Stmt::Define {
                    name,
                    pos,
                    ty,
                    var,
                    value,
                } => {
                    let defined = body.names.iter().position(|(n, _)| n == name);
                    match (defined, ty, var) {
                        (Some(at), None, false) => {
                            let needed = body.names[at].1.clone();
                            if body.vars.contains(name) {
                                let gives = self.expr(body, value, &Want::Ty(needed.clone()));
                                self.expect(body, value, &gives, &needed);
                            } else {
                                self.expr(body, value, &Want::Any);
                                self.errors.push(not_a_var(body, name, *pos, at));
                            }
                        }
                        _ => {
                            let bound = match ty {
                                Some(t) => {
                                    let needed = self.ty(t, body.scope);
                                    let gives = self.expr(body, value, &Want::Ty(needed.clone()));
                                    self.expect(body, value, &gives, &needed);
                                    needed
                                }
                                None => self.expr(body, value, &Want::Any),
                            };
                            let bound = self.not_stored(value, bound);
                            if defined.is_some() {
                                self.errors.push(self.defined_twice(body, name, *pos));
                            } else {
                                body.names.push((name.clone(), bound));
                                if *var {
                                    body.vars.push(name.clone());
                                }
                            }
                        }
                    }
                    End::Binding(*pos)
                }
                Stmt::Assign {
                    name,
                    pos,
                    fields,
                    value,
                } => {
                    let needed = self.place(body, name, *pos, fields);
                    let want = needed.clone().map_or(Want::Any, Want::Ty);
                    let gives = self.expr(body, value, &want);
                    if let Some(needed) = needed {
                        self.expect(body, value, &gives, &needed);
                    }
                    End::Binding(*pos)
                }
                Stmt::Destructure { pattern, ty, value } => {
                    let gives = match ty {
                        Some(t) => {
                            let needed = self.ty(t, body.scope);
                            let gives = self.expr(body, value, &Want::Ty(needed.clone()));
                            self.expect(body, value, &gives, &needed);
                            needed
                        }
                        None => self.expr(body, value, &Want::Any),
                    };
                    let gives = self.not_stored(value, gives);
                    let mut binds = Vec::new();
                    let mut assigned = Vec::new();
                    self.pattern(body, pattern, &gives, &mut binds, Some(&mut assigned));
                    body.define(binds);
                    End::Binding(pattern.pos())
                }
                Stmt::Expr(value) => {
                    let want = if is_last { last } else { &Want::Unused };
                    End::Value(self.expr(body, value, want), value)
                }
            };
        }
        end
    }

    /// Reports a value of type `ty` that holds a function, which a binding
    /// cannot store yet, and gives the type the binding then has.
    fn not_stored(&mut self, value: &Expr, ty: Ty) -> Ty {
        if ty.holds_fn() {
            self.errors.push(Error::unsupported(
                value.pos(),
                "a function stored in a binding",
            ));
            return Ty::Unknown;
        }
        ty
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
                    "there is no name `{name}` defined before this{}",
                    body.in_owner()
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

    /// Checks an expression against what is wanted of it, and gives its
    /// type, which is recorded.
    fn expr(&mut self, body: &mut Body<'a>, expr: &Expr, want: &Want) -> Ty {
        let ty = self.expr_of(body, expr, want);
        self.types.insert(addr(expr), ty.clone());
        if let Expr::Call(call) = expr {
            self.types.insert(addr(call), ty.clone());
        }
        ty
    }

    fn expr_of(&mut self, body: &mut Body<'a>, expr: &Expr, want: &Want) -> Ty {
        let defs = self.defs;
        match expr {
            Expr::Text { .. } => Ty::Text,
            Expr::Interp { pos, parts } => {
                let needs = "a string that holds an expression";
                if body.default.is_some() {
                    self.errors.push(Error::new(
                        *pos,
                        format!(
                            "a default carries no effect, and {needs} needs the `alloc` effect"
                        ),
                    ));
                    for part in parts {
                        if let Part::Expr(Expr::Call(call)) = part {
                            self.unknown_call(body, call);
                        }
                    }
                    return Ty::Unknown;
                }
                self.needs(body, "alloc", *pos, needs);
                for part in parts {
                    if let Part::Expr(e) = part {
                        self.expr(body, e, &Want::Ty(Ty::Text));
                    }
                }
                Ty::Text
            }
            Expr::Name { name, pos } => {
                if let Some((_, ty)) = body.names.iter().rfind(|(n, _)| n == name) {
                    ty.clone()
                } else if let (Some(&f), None) =
                    (defs.function_names.get(name.as_str()), &body.default)
                {
                    self.function_value(expr, f, *pos)
                } else {
                    let message = match &body.default {
                        Some(params) if params.contains(&name.as_str()) => {
                            format!("a default uses no parameter, and `{name}` is one")
                        }
                        Some(_) => format!("there is no name `{name}` for a default to use"),
                        None => format!(
                            "there is no name `{name}` defined before this{}",
                            body.in_owner()
                        ),
                    };
                    self.errors.push(Error::new(*pos, message));
                    Ty::Unknown
                }
            }
            Expr::Call(call) => self.call(body, call, want),
            Expr::Field { of, name, pos } => {
                if let Some(e) = defs.qualified_case(of, &body.names) {
                    return self.case(body, Some(e), name, of.pos(), *pos, None, want);
                }
                match self.expr(body, of, &Want::Any) {
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
                }
            }
            Expr::Case { name, pos, payload } => {
                let at = Pos {
                    line: pos.line,
                    col: pos.col + 1,
                };
                self.case(body, None, name, *pos, at, payload.as_deref(), want)
            }
            Expr::Match(m) => self.match_expr(body, m, want),
            Expr::Lambda(l) => match want {
                Want::Ty(Ty::Fn(items)) => self.lambda(body, l, items),
                Want::Ty(Ty::Unknown) => {
                    let unknown = vec![Ty::Unknown; l.params.len() + 1];
                    self.lambda(body, l, &unknown);
                    Ty::Unknown
                }
                Want::Ty(t) => {
                    let needed = defs.describe(t, body.scope);
                    self.errors.push(Error::new(
                        l.pos,
                        format!("this is a lambda, where {needed} is needed"),
                    ));
                    Ty::Unknown
                }
                Want::Any | Want::Unused => {
                    self.errors
                        .push(Error::new(l.pos, "a lambda is only passed to a call"));
                    Ty::Unknown
                }
            },
            Expr::Tuple { items, .. } => {
                // What is wanted of each element, where it is known.
                let wants: Vec<Want> = match want {
                    Want::Ty(Ty::Tuple(of)) if of.len() == items.len() => (of.iter())
                        .map(|t| match t {
                            Ty::Unknown => Want::Any,
                            t => Want::Ty(t.clone()),
                        })
                        .collect(),
                    Want::Ty(Ty::Unknown) => vec![Want::Ty(Ty::Unknown); items.len()],
                    _ => vec![Want::Any; items.len()],
                };
                let items = (items.iter().zip(&wants))
                    .map(|(item, want)| self.expr(body, item, want))
                    .collect();
                Ty::tuple(items)
            }
        }
    }

    /// Checks a lambda passed where a function is needed whose parameters
    /// and result, the result last, are of the types `wanted`, which may be
    /// partly known, and gives its type. Its body is checked where it is
    /// written, against the effects of the function it is written in, and
    /// it may assign that function's `var`s.
    fn lambda(&mut self, body: &mut Body<'a>, l: &Lambda, wanted: &[Ty]) -> Ty {
        let defs = self.defs;
        let (result, params) = wanted.split_last().expect("a result");
        if l.params.len() != params.len() {
            let needed = Ty::Fn(wanted.to_vec());
            self.errors.push(Error::new(
                l.pos,
                format!(
                    "this lambda takes {}, where {} is needed, which takes {}",
                    count(l.params.len(), "parameter"),
                    defs.describe(&needed, body.scope),
                    params.len()
                ),
            ));
            return Ty::Unknown;
        }
        let outer = body.names.len();
        let vars = body.vars.len();
        let mut types = Vec::new();
        for (i, ((name, pos), ty)) in l.params.iter().zip(params).enumerate() {
            let twice = l.params[..i].iter().any(|(n, _)| n == name)
                || body.names.iter().any(|(n, _)| n == name);
            if twice {
                self.errors.push(self.defined_twice(body, name, *pos));
            }
            types.push(ty.clone());
            body.names.push((name.clone(), ty.clone()));
        }
        let want = match result {
            Ty::Nothing => Want::Unused,
            t if t.unknown() => Want::Any,
            t => Want::Ty(t.clone()),
        };
        let end = self.block(body, &l.body, &want);
        let gives = match (end, result) {
            (End::Value(gives, value), Ty::Nothing) => {
                if !matches!(gives, Ty::Nothing | Ty::Unknown) {
                    self.expect(body, value, &gives, result);
                }
                Ty::Nothing
            }
            (_, Ty::Nothing) => Ty::Nothing,
            (End::Value(gives, value), result) if !result.unknown() => {
                self.expect(body, value, &gives, result);
                result.clone()
            }
            (End::Value(gives, _), _) => gives,
            (End::Empty, result) if !result.unknown() => {
                let needed = defs.describe(result, body.scope);
                self.errors.push(Error::new(
                    l.end,
                    format!(
                        "this lambda's body is empty, which gives nothing, where {needed} is needed"
                    ),
                ));
                Ty::Unknown
            }
            (End::Binding(pos), result) if !result.unknown() => {
                let needed = defs.describe(result, body.scope);
                self.errors.push(Error::new(
                    pos,
                    format!(
                        "this lambda ends with a binding or an assignment, which gives nothing, \
                         where {needed} is needed"
                    ),
                ));
                Ty::Unknown
            }
            (End::Empty | End::Binding(_), _) => Ty::Nothing,
        };
        body.names.truncate(outer);
        body.vars.truncate(vars);
        types.push(gives);
        Ty::func(types)
    }

    /// Checks the name of function `f`, at `pos`, passed as a value, and
    /// gives its type. A function type states no effect, so the function
    /// carries none.
    fn function_value(&mut self, expr: &Expr, f: usize, pos: Pos) -> Ty {
        let defs = self.defs;
        let sig = &defs.signatures[f];
        let name = &defs.file.functions[f].name;
        let error = if !sig.scope.names.is_empty() {
            Error::unsupported(pos, "passing a function with type parameters as a value")
        } else if sig.params.iter().any(|(_, t)| t.holds_fn()) {
            Error::unsupported(pos, "passing a function that takes a function as a value")
        } else if let Some(effect) = sig.effects.first() {
            Error::new(
                pos,
                format!(
                    "`{name}` carries the `{effect}` effect, and a function passed as a value \
                     carries none"
                ),
            )
        } else {
            self.fn_values.insert(addr(expr), f);
            let mut items: Vec<Ty> = sig.params.iter().map(|(_, t)| t.clone()).collect();
            items.push(sig.gives.clone());
            return Ty::func(items);
        };
        self.errors.push(error);
        Ty::Unknown
    }

    /// Checks a call of a name that holds a function whose parameters and
    /// result, the result last, are of the types `items`. Its arguments are
    /// given by position, and it carries no effect.
    fn value_call(&mut self, body: &mut Body<'a>, call: &Call, items: &[Ty]) -> Ty {
        self.value_calls.insert(addr(call));
        let (gives, params) = items.split_last().expect("a result");
        let name = &call.callee;
        let mut given = vec![false; params.len()];
        let mut next = 0;
        for arg in &call.args {
            let at = if arg.after {
                params.len().checked_sub(1)
            } else {
                next += 1;
                Some(next - 1).filter(|i| *i < params.len())
            };
            let error = if let Some((_, pos)) = &arg.name {
                Some(Error::new(
                    *pos,
                    format!(
                        "the arguments of `{name}` are given by position, as it is a function a name holds"
                    ),
                ))
            } else {
                match at {
                    Some(i) if given[i] => Some(Error::new(
                        arg.value.pos(),
                        format!("the argument for the last parameter of `{name}` is given twice"),
                    )),
                    Some(i) if arg.after && !matches!(params[i], Ty::Fn(_) | Ty::Unknown) => {
                        Some(Error::new(
                            arg.value.pos(),
                            format!(
                                "a lambda after a call is for its last parameter, and that of \
                                 `{name}` takes no function"
                            ),
                        ))
                    }
                    Some(_) => None,
                    None => Some(Error::new(
                        arg.value.pos(),
                        format!(
                            "`{name}` takes {}, and this is one more",
                            count(params.len(), "argument")
                        ),
                    )),
                }
            };
            if let Some(error) = error {
                self.errors.push(error);
                self.expr(body, &arg.value, &Want::Ty(Ty::Unknown));
                continue;
            }
            let i = at.expect("a parameter");
            given[i] = true;
            let value = self.expr(body, &arg.value, &Want::Ty(params[i].clone()));
            self.expect(body, &arg.value, &value, &params[i]);
        }
        let missing = given.iter().filter(|g| !**g).count();
        if missing > 0 {
            self.errors.push(Error::new(
                call.end,
                format!(
                    "`{name}` takes {}, and this call gives {}",
                    count(params.len(), "argument"),
                    params.len() - missing
                ),
            ));
        }
        gives.clone()
    }

    /// Reports a type given for a type parameter that holds a function,
    /// and gives whether there is one.
    fn function_for_parameter(&mut self, sol: &[Option<Ty>], pos: Pos) -> bool {
        if sol.iter().flatten().any(Ty::holds_fn) {
            self.errors.push(Error::unsupported(
                pos,
                "a function given for a type parameter",
            ));
            return true;
        }
        false
    }

    /// Checks a case named `name`: of enum `e` if it is written in full, or
    /// else of the enum `want` expects. `pos` is where it starts, and `at`
    /// where its name is.
    #[allow(clippy::too_many_arguments)]
    fn case(
        &mut self,
        body: &mut Body<'a>,
        e: Option<usize>,
        name: &str,
        pos: Pos,
        at: Pos,
        payload: Option<&Expr>,
        want: &Want,
    ) -> Ty {
        let defs = self.defs;
        let quiet = Want::Ty(Ty::Unknown);
        let expected = match want {
            Want::Ty(t) => Some(t),
            Want::Any | Want::Unused => None,
        };
        let error = |checker: &mut Self, body: &mut Body<'a>, error: Option<Error>| {
            checker.errors.extend(error);
            if let Some(p) = payload {
                checker.expr(body, p, &quiet);
            }
            Ty::Unknown
        };
        let e = match (e, expected) {
            (Some(e), _) => e,
            (None, Some(Ty::Enum(e, _))) => *e,
            (None, Some(Ty::Unknown)) => return error(self, body, None),
            (None, Some(t)) => {
                let needed = defs.describe(t, body.scope);
                let message = format!("`.{name}` is a case of an enum, where {needed} is needed");
                return error(self, body, Some(Error::new(pos, message)));
            }
            (None, None) => {
                let message = format!("the enum of `.{name}` is not known here");
                let fix = "write the enum's name before the dot, or give the binding a type";
                return error(self, body, Some(Error::new(pos, message).with_fix(fix)));
            }
        };
        let def = &defs.file.enums[e];
        let Some(k) = defs.case_of(e, name) else {
            let message = format!("`{}` has no case named `{name}`", def.name);
            return error(self, body, Some(Error::new(at, message)));
        };
        let vars = &defs.enum_scopes[e];
        let mut sol: Vec<Option<Ty>> = match expected {
            Some(Ty::Enum(f, args)) if *f == e => (args.iter())
                .map(|t| (!t.unknown()).then(|| t.clone()))
                .collect(),
            _ => vec![None; def.generics.len()],
        };
        let before = self.errors.len();
        match (&defs.cases[e][k], payload) {
            (Some(pattern), Some(p)) => {
                let gives = self.expr(body, p, &Want::Ty(pattern.partial(&sol)));
                if gives.unknown() {
                    return Ty::Unknown;
                }
                self.fit(body, p, &gives, pattern, &mut sol, vars);
            }
            (Some(pattern), None) => {
                let holds = defs.describe_pattern(pattern, &sol, vars, body.scope);
                let message = format!("`.{name}` holds {holds}, and none is given");
                return error(self, body, Some(Error::new(pos, message)));
            }
            (None, Some(p)) => {
                let message = format!("`.{name}` holds nothing, so it takes no value");
                return error(self, body, Some(Error::new(p.pos(), message)));
            }
            (None, None) => {}
        }
        if self.errors.len() > before || self.function_for_parameter(&sol, pos) {
            return Ty::Unknown;
        }
        if let Some(g) = sol.iter().position(Option::is_none) {
            self.errors.push(Error::new(
                pos,
                format!(
                    "the type `{}` of `{}` is not known here",
                    def.generics[g].name, def.name
                ),
            ));
            return Ty::Unknown;
        }
        if defs.cyclic_enums[e] {
            return Ty::Unknown;
        }
        Ty::enumeration(e, sol.into_iter().map(|t| t.expect("solved")).collect())
    }

    /// Checks a `match`: the value it takes apart, each pattern against
    /// the value's type, each block, and that the patterns cover every
    /// value once with none that is never reached. Used as an expression,
    /// its blocks give the same type, which it gives.
    fn match_expr(&mut self, body: &mut Body<'a>, m: &Match, want: &Want) -> Ty {
        let defs = self.defs;
        let of = self.expr(body, &m.of, &Want::Any);
        let outer = body.names.len();
        let vars = body.vars.len();
        let mut patterns: Vec<Option<Pat>> = Vec::new();
        let mut first: Option<Ty> = None;
        for arm in &m.arms {
            let mut binds = Vec::new();
            let pattern = self.pattern(body, &arm.pattern, &of, &mut binds, None);
            if let Some(p) = &pattern
                && !of.unknown()
                && patterns.iter().all(Option::is_some)
            {
                let before: Vec<&Pat> = patterns.iter().flatten().collect();
                if !useful(defs, &before, p, &of) {
                    self.errors.push(Error::new(
                        arm.pattern.pos(),
                        "this pattern is never reached, as the patterns before it match \
                         everything it matches",
                    ));
                }
            }
            patterns.push(pattern);
            body.define(binds);
            let arm_want = match (want, &first) {
                (Want::Unused, _) => Want::Unused,
                (Want::Ty(t), _) => Want::Ty(t.clone()),
                (Want::Any, Some(t)) => Want::Ty(t.clone()),
                (Want::Any, None) => Want::Any,
            };
            let end = self.block(body, &arm.body, &arm_want);
            if !matches!(want, Want::Unused) {
                let gives = self.arm_gives(body, arm, end, want, &first);
                first = first.or(Some(gives));
            }
            body.names.truncate(outer);
            body.vars.truncate(vars);
        }
        if !of.unknown() && patterns.iter().all(Option::is_some) {
            let all: Vec<&Pat> = patterns.iter().flatten().collect();
            let left = missing(defs, &all, &of);
            if !left.is_empty() {
                let left: Vec<&str> = left.iter().map(String::as_str).collect();
                let blocks = if left.len() == 1 { "a block" } else { "blocks" };
                let left = names(&left);
                self.errors.push(
                    Error::new(m.pos, format!("this `match` does not cover {left}")).with_fix(
                        format!(
                            "add {blocks} for {left}, or end with a bare name that covers the rest"
                        ),
                    ),
                );
            }
        }
        match want {
            Want::Unused => Ty::Nothing,
            Want::Ty(t) if !t.unknown() => t.clone(),
            _ => first.unwrap_or(Ty::Nothing),
        }
    }

    /// Checks what the block of an arm gives, where the `match` is used as
    /// an expression, against what is wanted of it or what the first block
    /// gives, and gives it.
    fn arm_gives(
        &mut self,
        body: &Body,
        arm: &Arm,
        end: End,
        want: &Want,
        first: &Option<Ty>,
    ) -> Ty {
        let defs = self.defs;
        let needed = match (want, first) {
            (Want::Ty(t), _) => Some(t),
            (_, Some(t)) => Some(t),
            _ => None,
        };
        let describe = |t: &Ty| defs.describe(t, body.scope);
        let (gives, at) = match &end {
            End::Value(gives, value) => (gives.clone(), value.pos()),
            End::Empty => (Ty::Nothing, arm.end),
            End::Binding(pos) => (Ty::Nothing, *pos),
        };
        let Some(needed) = needed else {
            return gives;
        };
        if agrees(needed, &gives) {
            return gives;
        }
        let what = match (&end, &gives) {
            (End::Empty, _) => "this block is empty, which gives nothing".to_string(),
            (End::Binding(_), _) => {
                "this block ends with a binding or an assignment, which gives nothing".to_string()
            }
            (End::Value(_, Expr::Call(call)), Ty::Nothing) => {
                format!("`{}` returns nothing", call.callee)
            }
            _ => format!("this gives {}", describe(&gives)),
        };
        let message = match want {
            Want::Ty(t) => format!("{what}, where {} is needed", describe(t)),
            _ => format!(
                "{what}, and the first block gives {}, where the blocks of a `match` give the \
                 same type",
                describe(needed)
            ),
        };
        self.errors.push(Error::new(at, message));
        gives
    }

    /// Checks a pattern against the type of what it matches, and gives it
    /// as checked, or `None` if it holds a mistake. Each bare name it
    /// defines goes in `binds`, with the type of what it names and whether
    /// it is a `var`.
    ///
    /// In a `match`, `assigned` is `None` and a bare name always defines a
    /// name. In a binding, a bare name that is already a `var` is assigned
    /// and goes in `assigned`, and a case is not built yet.
    fn pattern(
        &mut self,
        body: &Body,
        pattern: &Pattern,
        ty: &Ty,
        binds: &mut Vec<(String, Ty, bool)>,
        mut assigned: Option<&mut Vec<String>>,
    ) -> Option<Pat> {
        let defs = self.defs;
        let quietly = |checker: &mut Self,
                       binds: &mut Vec<(String, Ty, bool)>,
                       assigned: Option<&mut Vec<String>>,
                       inner: &[&Pattern]| {
            let mut assigned = assigned;
            for inner in inner {
                checker.pattern(body, inner, &Ty::Unknown, binds, assigned.as_deref_mut());
            }
            None
        };
        match pattern {
            Pattern::Bind { name, pos, var } => {
                let defined = body.names.iter().position(|(n, _)| n == name);
                let in_pattern = binds.iter().any(|(n, ..)| n == name);
                let is_var = body.vars.contains(name);
                match (defined, &mut assigned) {
                    (Some(at), Some(assigned)) if var.is_none() && !in_pattern => {
                        if !is_var {
                            self.errors.push(not_a_var(body, name, *pos, at));
                            return None;
                        }
                        if assigned.contains(name) {
                            self.errors.push(Error::unsupported(
                                *pos,
                                "assigning to a `var` twice in one pattern",
                            ));
                            return None;
                        }
                        assigned.push(name.clone());
                        let holds = &body.names[at].1;
                        if !agrees(holds, ty) {
                            self.errors.push(Error::new(
                                *pos,
                                format!(
                                    "`{name}` is a `var` that holds {}, and this pattern gives \
                                     it {}",
                                    defs.describe(holds, body.scope),
                                    defs.describe(ty, body.scope)
                                ),
                            ));
                            return None;
                        }
                        Some(Pat::Any)
                    }
                    _ if defined.is_some() || in_pattern => {
                        // The name stands for what it matches all the same, as
                        // not known, so that nothing it explains is reported.
                        self.errors.push(self.defined_twice(body, name, *pos));
                        binds.push((name.clone(), Ty::Unknown, var.is_some()));
                        None
                    }
                    _ => {
                        binds.push((name.clone(), ty.clone(), var.is_some()));
                        Some(Pat::Any)
                    }
                }
            }
            Pattern::Tuple { pos, items } => {
                let inner: Vec<&Pattern> = items.iter().collect();
                let of = match ty {
                    Ty::Tuple(of) if of.len() == items.len() => of,
                    Ty::Unknown => return quietly(self, binds, assigned, &inner),
                    other => {
                        self.errors.push(Error::new(
                            *pos,
                            format!(
                                "this pattern takes apart a tuple of {}, and it matches {}",
                                items.len(),
                                defs.describe(other, body.scope)
                            ),
                        ));
                        return quietly(self, binds, assigned, &inner);
                    }
                };
                let mut out = Some(Vec::new());
                for (item, t) in items.iter().zip(of) {
                    let item = self.pattern(body, item, t, binds, assigned.as_deref_mut());
                    out = out.zip(item).map(|(mut out, item)| {
                        out.push(item);
                        out
                    });
                }
                out.map(Pat::Tuple)
            }
            Pattern::Case { name, pos, inner } => {
                let inner_list: Vec<&Pattern> = inner.as_deref().into_iter().collect();
                if assigned.is_some() {
                    self.errors
                        .push(Error::unsupported(*pos, "a case in a binding's pattern"));
                    return quietly(self, binds, assigned, &inner_list);
                }
                let (e, args) = match ty {
                    Ty::Enum(e, args) => (*e, args),
                    Ty::Unknown => return quietly(self, binds, assigned, &inner_list),
                    other => {
                        self.errors.push(Error::new(
                            *pos,
                            format!(
                                "`.{name}` is a case, and this matches {}, which is not an enum",
                                defs.describe(other, body.scope)
                            ),
                        ));
                        return quietly(self, binds, assigned, &inner_list);
                    }
                };
                let Some(k) = defs.case_of(e, name) else {
                    let at = Pos {
                        line: pos.line,
                        col: pos.col + 1,
                    };
                    self.errors.push(Error::new(
                        at,
                        format!("`{}` has no case named `{name}`", defs.file.enums[e].name),
                    ));
                    return quietly(self, binds, assigned, &inner_list);
                };
                match (&defs.cases[e][k], inner) {
                    (Some(holds), Some(inner)) => {
                        let inner = self.pattern(body, inner, &holds.subst(args), binds, None)?;
                        Some(Pat::Case(k, Some(Box::new(inner))))
                    }
                    (Some(holds), None) => {
                        self.errors.push(
                            Error::new(
                                *pos,
                                format!(
                                    "`.{name}` holds {}, and its pattern gives none for it",
                                    defs.describe(&holds.subst(args), body.scope)
                                ),
                            )
                            .with_fix(format!("write `.{name}(x)` to name what it holds")),
                        );
                        None
                    }
                    (None, Some(inner)) => {
                        self.errors.push(Error::new(
                            inner.pos(),
                            format!("`.{name}` holds nothing, so its pattern takes none"),
                        ));
                        None
                    }
                    (None, None) => Some(Pat::Case(k, None)),
                }
            }
        }
    }

    /// Checks the arguments of a call that names nothing there is, for the
    /// mistakes in them.
    fn unknown_call(&mut self, body: &mut Body<'a>, call: &Call) -> Ty {
        for arg in &call.args {
            self.expr(body, &arg.value, &Want::Ty(Ty::Unknown));
        }
        Ty::Unknown
    }

    /// Checks a case of enum `e` written in full with parentheses, as in
    /// `Pet.cat(c)`, which gives the value the case holds by position.
    fn qualified_case(&mut self, body: &mut Body<'a>, call: &Call, e: usize, want: &Want) -> Ty {
        let quiet = Want::Ty(Ty::Unknown);
        let mut ok = true;
        for (i, arg) in call.args.iter().enumerate() {
            if let Some((_, pos)) = &arg.name {
                self.errors.push(Error::new(
                    *pos,
                    "the value a case holds is given by position",
                ));
                ok = false;
            } else if i > 0 {
                self.errors.push(Error::new(
                    arg.value.pos(),
                    "a case holds one value, and this is one more",
                ));
                ok = false;
            }
        }
        if !ok {
            for arg in &call.args {
                self.expr(body, &arg.value, &quiet);
            }
            return Ty::Unknown;
        }
        let payload = call.args.first().map(|a| &a.value);
        let (callee, pos, at) = (&call.callee, call.pos, call.name_pos);
        self.case(body, Some(e), callee, pos, at, payload, want)
    }

    fn call(&mut self, body: &mut Body<'a>, call: &Call, want: &Want) -> Ty {
        let defs = self.defs;
        let name = call.callee.as_str();
        if let Some((q, at)) = &call.qualifier {
            if let Some(&e) = defs.enum_names.get(q.as_str())
                && !body.names.iter().any(|(n, _)| n == q)
            {
                return self.qualified_case(body, call, e, want);
            }
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
        if let Some((_, Ty::Fn(items))) = body.names.iter().rfind(|(n, _)| n == name) {
            let items = items.clone();
            return self.value_call(body, call, &items);
        }
        if let Some(&s) = defs.struct_names.get(name) {
            return self.construction(body, call, s, want);
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
            if self.function_for_parameter(&sol, call.pos) {
                return Ty::Unknown;
            }
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
        if self.function_for_parameter(&sol, call.pos) {
            return Ty::Unknown;
        }
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
    fn construction(&mut self, body: &mut Body<'a>, call: &Call, s: usize, want: &Want) -> Ty {
        let defs = self.defs;
        let def = &defs.file.structs[s];
        let vars = &defs.struct_scopes[s];
        let mut sol: Vec<Option<Ty>> = match want {
            Want::Ty(Ty::Struct(t, args)) if *t == s => (args.iter())
                .map(|t| (!t.unknown()).then(|| t.clone()))
                .collect(),
            _ => vec![None; def.generics.len()],
        };
        let mut given = vec![false; def.fields.len()];
        let mut by_position = false;
        let before = self.errors.len();
        for arg in &call.args {
            if let Expr::Lambda(l) = &arg.value {
                self.errors.push(Error::new(
                    l.pos,
                    "a lambda cannot be stored in a field, as it is only passed to a call",
                ));
                let field = (arg.name.as_ref())
                    .and_then(|(name, _)| def.fields.iter().position(|f| &f.name == name));
                if let Some(i) = field {
                    given[i] = true;
                }
                by_position |= arg.name.is_none() && !arg.after;
                continue;
            }
            let field = (arg.name.as_ref())
                .and_then(|(name, _)| def.fields.iter().position(|f| &f.name == name));
            let wanted = match field {
                Some(i) => Want::Ty(defs.fields[s][i].partial(&sol)),
                None => Want::Ty(Ty::Unknown),
            };
            let gives = self.expr(body, &arg.value, &wanted);
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
        if self.errors.len() == before && self.function_for_parameter(&sol, call.pos) {
            return Ty::Unknown;
        }
        if self.errors.len() == before
            && let Some(g) = sol.iter().position(Option::is_none)
        {
            self.errors.push(Error::new(
                call.pos,
                format!(
                    "the type `{}` of `{}` is not known here",
                    def.generics[g].name, def.name
                ),
            ));
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
            self.needs(body, effect, call.pos, &format!("`{}`", call.callee));
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
            let callee = &call.callee;
            if arg.after {
                // A lambda after a call is for its last parameter.
                let last = signature.params.len().checked_sub(1);
                let error = match last {
                    None => Some(format!(
                        "`{callee}` takes no arguments, so no lambda goes after the call"
                    )),
                    Some(i) if !matches!(signature.params[i].1, Ty::Fn(_) | Ty::Unknown) => {
                        // The parameter is not then reported missing.
                        given[i] = true;
                        Some(format!(
                            "a lambda after a call is for its last parameter, and `{}`, the last \
                             parameter of `{callee}`, takes no function",
                            signature.params[i].0
                        ))
                    }
                    Some(i) if given[i] => Some(format!(
                        "the `{}` argument is given twice",
                        signature.params[i].0
                    )),
                    Some(i) => {
                        given[i] = true;
                        param = Some(i);
                        None
                    }
                };
                if let Some(message) = error {
                    self.errors.push(Error::new(arg.value.pos(), message));
                    continue;
                }
            } else {
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
            }
            if !look_into {
                continue;
            }
            if let (Some(i), Expr::Lambda(l)) = (param, &arg.value) {
                let pattern = &signature.params[i].1;
                if !matches!(pattern, Ty::Fn(_) | Ty::Unknown) {
                    let needed = self.defs.describe_pattern(pattern, &sol, vars, body.scope);
                    self.errors.push(Error::new(
                        l.pos,
                        format!("this is a lambda, where {needed} is needed"),
                    ));
                    continue;
                }
            }
            let wanted = match param {
                Some(i) => Want::Ty(signature.params[i].1.partial(&sol)),
                None => Want::Ty(Ty::Unknown),
            };
            let gives = self.expr(body, &arg.value, &wanted);
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

    /// Reports that what is at `pos`, which an error names as `what`, needs
    /// an effect the body does not list, unless that is already reported or
    /// a listed effect with no such name may be it misspelt.
    fn needs(&mut self, body: &mut Body, effect: &str, pos: Pos, what: &str) {
        if body.listed.contains(&effect)
            || body.unknown_effect
            || body.missing.iter().any(|m| m == effect)
        {
            return;
        }
        body.missing.push(effect.to_string());
        let mut effects = body.listed.clone();
        effects.extend(body.missing.iter().map(String::as_str));
        self.errors.push(
            Error::new(
                pos,
                format!(
                    "{what} needs the `{effect}` effect, which `{}` does not declare",
                    body.owner().name
                ),
            )
            .with_fix(format!("write `{}`", written(body.owner(), &effects))),
        );
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
        // The functions passed as values, which may be called.
        let mut passed = Vec::new();
        for e in exprs(values(&defs.file.functions[f].body)) {
            if let Some(&g) = self.fn_values.get(&addr(e)) {
                passed.push((e.pos(), g));
            }
            let Expr::Call(call) = e else {
                continue;
            };
            if self.value_calls.contains(&addr(call)) {
                continue;
            }
            all.push(call);
            defs.defaults_called(call, &mut Vec::new(), &mut all, &mut cycles);
        }
        self.errors.extend(cycles.into_iter().map(default_cycle));
        let called = (all.into_iter())
            .filter(|call| call.qualifier.is_none())
            .filter_map(|call| {
                let callee = self.defs.function_names.get(call.callee.as_str())?;
                Some((call.pos, *callee))
            });
        let reached: Vec<(Pos, usize)> = called.chain(passed).collect();
        for (pos, callee) in reached {
            if path.contains(&callee) {
                self.errors.push(Error::unsupported(
                    pos,
                    "a function that calls itself, directly or through others,",
                ));
            } else if !done.contains(&callee) {
                self.recursion(callee, path, done);
            }
        }
        path.pop();
        done.push(f);
    }
}

/// Lowers checked functions to the form every target receives, one copy
/// of a function for each choice of its type parameters that runs.
///
/// A function that takes a function is not lowered on its own. Each call
/// of it is lowered in place, with its parameters standing for the
/// arguments, and a call of a parameter that holds a lambda lowers the
/// lambda's body in place, where the names of the function it is written
/// in are as they are at that moment. So the form every target receives
/// holds no lambda and no value of a function type.
struct Lowering<'a> {
    typed: &'a Typed<'a>,
    /// The copies, each at its place in the program, in the order they are
    /// first called.
    instances: Vec<Instance>,
    texts: Vec<Vec<u8>>,
}

/// A function being lowered.
struct Lowered<'a> {
    /// The names defined so far in the function, which is the first frame,
    /// and in each function lowered in place inside it, one frame each.
    frames: Vec<Vec<Local<'a>>>,
    /// The frame of the code being lowered.
    at: usize,
    locals: usize,
    steps: Vec<Step>,
}

/// A name being lowered: what it gives, the values it stands for, whether
/// it is a `var`, and the function it holds, if it holds one.
#[derive(Clone)]
struct Local<'a> {
    name: String,
    ty: Ty,
    values: Vec<Value>,
    var: bool,
    func: Option<Func<'a>>,
}

/// The function a name holds.
#[derive(Clone)]
enum Func<'a> {
    /// A lambda, the frame it is written in, and the types that stand for
    /// the type parameters of the definition it is written in.
    Lambda(&'a Lambda, usize, Vec<Ty>),
    /// A copy of a function with a name.
    Named(Instance),
}

impl<'a> Lowered<'a> {
    fn names(&self) -> &Vec<Local<'a>> {
        &self.frames[self.at]
    }

    fn names_mut(&mut self) -> &mut Vec<Local<'a>> {
        &mut self.frames[self.at]
    }

    /// The place of a name among those of the frame being lowered.
    fn find(&self, name: &str) -> Option<usize> {
        self.names().iter().rposition(|l| l.name == name)
    }

    /// Defines a name that stands for values.
    fn define(&mut self, name: &str, ty: Ty, values: Vec<Value>, var: bool) {
        self.names_mut().push(Local {
            name: name.to_string(),
            ty,
            values,
            var,
            func: None,
        });
    }
}

/// Patterns still to be matched, one for each value, where `None` matches
/// anything.
type Patterns<'p> = Vec<Option<&'p Pattern>>;

/// A pattern still to be matched, as a decision is made between the
/// blocks of a `match`: the block it leads to, what is left of the pattern,
/// one for each value still to look at, each of which matches anything
/// when it is `None`, and the names bound so far, each with its type and
/// its values.
#[derive(Clone)]
struct Row<'p> {
    arm: usize,
    patterns: Patterns<'p>,
    binds: Vec<(String, Ty, Vec<Value>)>,
}

/// What every block of a `match` being lowered shares: its blocks, the
/// types that stand for the type parameters, the locals its value goes to,
/// and the names defined before it that a block assigns to, each with the
/// locals its values go to.
struct Arms<'a, 'p> {
    arms: &'a [Arm],
    env: &'p [Ty],
    results: Vec<usize>,
    /// Each name by its frame and its place there, with its locals.
    merged: Vec<((usize, usize), Vec<usize>)>,
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

    /// How many values a value of the type is lowered to: one for each
    /// text, those of each element for a tuple, for an enum one for its case
    /// and as many as its largest case holds, and none for a function,
    /// whose calls are lowered in place.
    fn width(&self, ty: &Ty) -> usize {
        let defs = self.typed.defs;
        match ty {
            Ty::Text => 1,
            Ty::Struct(s, args) => (defs.fields[*s].iter())
                .map(|f| self.width(&f.subst(args)))
                .sum(),
            Ty::Enum(e, args) => {
                let holds = (defs.cases[*e].iter().flatten())
                    .map(|t| self.width(&t.subst(args)))
                    .max();
                1 + holds.unwrap_or(0)
            }
            Ty::Tuple(items) => items.iter().map(|t| self.width(t)).sum(),
            Ty::Param(_) | Ty::Fn(_) | Ty::Nothing | Ty::Unknown => 0,
        }
    }

    /// The values of each element of a tuple of the types `items`, from the
    /// values of the tuple.
    fn elements(&self, items: &[Ty], values: &[Value]) -> Vec<Vec<Value>> {
        let mut at = 0;
        (items.iter())
            .map(|t| {
                let width = self.width(t);
                at += width;
                values[at - width..at].to_vec()
            })
            .collect()
    }

    /// Makes each bare name in a pattern of a binding stand for the values
    /// it matches: a name defined before it, which is a `var`, from then on,
    /// and any other as a new name.
    fn bind(&self, out: &mut Lowered, pattern: &Pattern, ty: &Ty, values: Vec<Value>) {
        match pattern {
            Pattern::Bind { name, var, .. } => match out.find(name) {
                Some(at) => out.names_mut()[at].values = values,
                None => out.define(name, ty.clone(), values, var.is_some()),
            },
            Pattern::Tuple { items, .. } => {
                let Ty::Tuple(of) = ty else {
                    unreachable!("checked")
                };
                for ((item, t), values) in items.iter().zip(of).zip(self.elements(of, &values)) {
                    self.bind(out, item, t, values);
                }
            }
            Pattern::Case { .. } => unreachable!("checked"),
        }
    }

    /// Where the values of a field, or of a field of a field for each
    /// further name, are among the values of a value of the type, and how
    /// many there are.
    fn offset(&self, ty: &Ty, fields: &[(String, Pos)]) -> (usize, usize) {
        let mut ty = ty.clone();
        let mut start = 0;
        for (name, _) in fields {
            let Ty::Struct(s, args) = ty else {
                unreachable!("checked")
            };
            let defs = self.typed.defs;
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
    fn locals(&self, out: &mut Lowered<'a>, ty: &Ty) -> Vec<usize> {
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
        let defs = self.typed.defs;
        let view = defs.view(instance.template);
        let mut out = Lowered {
            frames: vec![Vec::new()],
            at: 0,
            locals: 0,
            steps: Vec::new(),
        };
        for (name, ty) in &view.params {
            let ty = ty.subst(&instance.args);
            let locals = self.locals(&mut out, &ty);
            let values = locals.into_iter().map(Value::Local).collect();
            out.define(name, ty, values, false);
        }
        let params = out.locals;
        let mut result = self.block(&mut out, &view.f.body, &instance.args);
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

    /// Lowers the statements of a block, and gives the values of the last
    /// if it is an expression.
    fn block(&mut self, out: &mut Lowered<'a>, stmts: &'a [Stmt], env: &[Ty]) -> Vec<Value> {
        let mut result = Vec::new();
        for stmt in stmts {
            result = match stmt {
                Stmt::Define {
                    name, var, value, ..
                } => {
                    // A name stands for the values it is bound to, so a
                    // binding needs no step of its own. A block runs its
                    // statements once each, in order, so an assignment
                    // makes the name stand for the new values from then
                    // on, and a name bound to the old ones keeps them.
                    let (ty, value) = self.expr(out, value, env);
                    match out.find(name) {
                        Some(at) => out.names_mut()[at].values = value,
                        None => out.define(name, ty, value, *var),
                    }
                    Vec::new()
                }
                Stmt::Assign {
                    name,
                    fields,
                    value,
                    ..
                } => {
                    let (_, value) = self.expr(out, value, env);
                    let at = out.find(name).expect("checked");
                    let (start, width) = self.offset(&out.names()[at].ty, fields);
                    out.names_mut()[at]
                        .values
                        .splice(start..start + width, value);
                    Vec::new()
                }
                Stmt::Destructure { pattern, value, .. } => {
                    let (ty, value) = self.expr(out, value, env);
                    self.bind(out, pattern, &ty, value);
                    Vec::new()
                }
                Stmt::Expr(value) => self.expr(out, value, env).1,
            };
        }
        result
    }

    /// The values an expression gives, and their type, where `env` stands
    /// for the type parameters of the definition it is written in.
    fn expr(&mut self, out: &mut Lowered<'a>, expr: &'a Expr, env: &[Ty]) -> (Ty, Vec<Value>) {
        let typed = self.typed;
        let defs = typed.defs;
        match expr {
            Expr::Text { text, .. } => (Ty::Text, vec![self.text(text)]),
            Expr::Interp { parts, .. } => {
                let parts = (parts.iter())
                    .map(|part| match part {
                        Part::Text(text) => self.text(text),
                        Part::Expr(e) => self.expr(out, e, env).1[0],
                    })
                    .collect();
                let local = self.locals(out, &Ty::Text)[0];
                out.steps.push(Step::Join { local, parts });
                (Ty::Text, vec![Value::Local(local)])
            }
            Expr::Name { name, .. } => match out.find(name) {
                Some(at) => (out.names()[at].ty.clone(), out.names()[at].values.clone()),
                // A function passed as a value is lowered to no values.
                None => (typed.type_of(expr, env), Vec::new()),
            },
            Expr::Call(call) => match typed.callee(call, env) {
                Callee::Construct(s, args) => {
                    let inner = args.clone();
                    let values = self.construction(out, call, s, env, &inner);
                    (Ty::Struct(s, args), values)
                }
                Callee::Case(k) => {
                    let ty = typed.type_of(expr, env);
                    let payload = call.args.first().map(|a| &a.value);
                    let values = self.case(out, &ty, k, payload, env);
                    (ty, values)
                }
                Callee::Print => {
                    let (_, text) = self.expr(out, &call.args[0].value, env);
                    let newline = self.text(b"\n");
                    for value in [text[0], newline] {
                        out.steps.push(Step::Write {
                            stream: Stream::Stdout,
                            value,
                        });
                    }
                    (Ty::Nothing, Vec::new())
                }
                Callee::Value => self.call_value(out, call, env),
                Callee::Run(instance, gives)
                    if (defs.view(instance.template).params.iter())
                        .any(|(_, t)| t.subst(&instance.args).holds_fn()) =>
                {
                    self.in_place(out, call, instance, gives, env)
                }
                Callee::Run(instance, gives) => {
                    let inner = typed.env_of(&Callee::Run(instance.clone(), gives.clone()));
                    let params = defs.view(instance.template).params;
                    let function = self.place(instance);
                    // Arguments are worked out in the order they are
                    // written, and then passed in the order of the
                    // parameters.
                    let mut args: Vec<Vec<Value>> = vec![Vec::new(); params.len()];
                    for (arg, at) in call.args.iter().zip(params_of(&params, &call.args)) {
                        args[at] = self.expr(out, &arg.value, env).1;
                    }
                    // Then the defaults it leaves out, in the order of the
                    // parameters, as if they were written after the rest.
                    for (at, default) in defs.left_out(call) {
                        args[at] = self.expr(out, default, &inner).1;
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
                let names: Vec<(String, Ty)> = (out.names().iter())
                    .map(|l| (l.name.clone(), l.ty.clone()))
                    .collect();
                if defs.qualified_case(of, &names).is_some() {
                    let ty = typed.type_of(expr, env);
                    let Ty::Enum(e, _) = ty else {
                        unreachable!("checked")
                    };
                    let k = defs.case_of(e, name).expect("checked");
                    let values = self.case(out, &ty, k, None, env);
                    return (ty, values);
                }
                let (Ty::Struct(s, args), values) = self.expr(out, of, env) else {
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
            Expr::Case { name, payload, .. } => {
                let ty = typed.type_of(expr, env);
                let Ty::Enum(e, _) = ty else {
                    unreachable!("checked")
                };
                let k = defs.case_of(e, name).expect("checked");
                let values = self.case(out, &ty, k, payload.as_deref(), env);
                (ty, values)
            }
            Expr::Match(m) => {
                let ty = typed.type_of(expr, env);
                let values = self.match_expr(out, m, &ty, env);
                (ty, values)
            }
            Expr::Tuple { items, .. } => {
                let mut types = Vec::new();
                let mut values = Vec::new();
                for item in items {
                    let (ty, value) = self.expr(out, item, env);
                    types.push(ty);
                    values.extend(value);
                }
                (Ty::tuple(types), values)
            }
            Expr::Lambda(_) => unreachable!("checked"),
        }
    }

    /// Lowers a call of a function that takes a function, in place: its
    /// body, with its parameters standing for the arguments, in a frame of
    /// its own.
    fn in_place(
        &mut self,
        out: &mut Lowered<'a>,
        call: &'a Call,
        instance: Instance,
        gives: Ty,
        env: &[Ty],
    ) -> (Ty, Vec<Value>) {
        let typed = self.typed;
        let defs = typed.defs;
        let inner = typed.env_of(&Callee::Run(instance.clone(), gives.clone()));
        let view = defs.view(instance.template);
        let params: Vec<(String, Ty)> = (view.params.iter())
            .map(|(n, t)| (n.clone(), t.subst(&instance.args)))
            .collect();
        // Arguments are worked out in the order they are written, and then
        // the defaults the call leaves out, as for any call.
        let mut args: Vec<Option<Local<'a>>> = vec![None; params.len()];
        for (arg, at) in call.args.iter().zip(params_of(&params, &call.args)) {
            args[at] = Some(self.argument(out, &arg.value, &params[at], env));
        }
        for (at, default) in defs.left_out(call) {
            args[at] = Some(self.argument(out, default, &params[at], &inner));
        }
        let frame = args.into_iter().map(|a| a.expect("given")).collect();
        out.frames.push(frame);
        let caller = std::mem::replace(&mut out.at, out.frames.len() - 1);
        let mut result = self.block(out, &view.f.body, &instance.args);
        out.frames.pop();
        out.at = caller;
        if gives == Ty::Nothing {
            result.clear();
        }
        (gives, result)
    }

    /// An argument for a parameter, as the name the parameter is then.
    fn argument(
        &mut self,
        out: &mut Lowered<'a>,
        value: &'a Expr,
        (name, ty): &(String, Ty),
        env: &[Ty],
    ) -> Local<'a> {
        let (values, func) = if let Ty::Fn(_) = ty {
            let func = match value {
                Expr::Lambda(l) => Func::Lambda(l, out.at, env.to_vec()),
                Expr::Name { name, .. } => match out.find(name) {
                    Some(at) => out.names()[at].func.clone().expect("a function"),
                    None => Func::Named(Instance {
                        template: Template::Free(self.typed.fn_values[&addr(value)]),
                        args: Vec::new(),
                    }),
                },
                _ => unreachable!("checked"),
            };
            (Vec::new(), Some(func))
        } else {
            (self.expr(out, value, env).1, None)
        };
        Local {
            name: name.clone(),
            ty: ty.clone(),
            values,
            var: false,
            func,
        }
    }

    /// Lowers a call of a name that holds a function. A lambda's body is
    /// lowered in place, in the frame it is written in, with its parameters
    /// standing for the arguments.
    fn call_value(
        &mut self,
        out: &mut Lowered<'a>,
        call: &'a Call,
        env: &[Ty],
    ) -> (Ty, Vec<Value>) {
        let at = out.find(&call.callee).expect("checked");
        let local = &out.names()[at];
        let func = local.func.clone().expect("a function");
        let Ty::Fn(items) = local.ty.clone() else {
            unreachable!("checked")
        };
        let (gives, params) = items.split_last().expect("a result");
        let mut args: Vec<Option<Local<'a>>> = vec![None; params.len()];
        let mut next = 0;
        for arg in &call.args {
            let at = if arg.after {
                params.len() - 1
            } else {
                next += 1;
                next - 1
            };
            let param = (String::new(), params[at].clone());
            args[at] = Some(self.argument(out, &arg.value, &param, env));
        }
        let args = args.into_iter().map(|a| a.expect("given"));
        let mut result = match func {
            Func::Named(instance) => {
                let function = self.place(instance);
                let args = args.flat_map(|a| a.values).collect();
                let results = self.locals(out, gives);
                out.steps.push(Step::Call {
                    function,
                    args,
                    results: results.clone(),
                });
                results.into_iter().map(Value::Local).collect()
            }
            Func::Lambda(lambda, frame, lambda_env) => {
                let caller = std::mem::replace(&mut out.at, frame);
                let before = out.names().len();
                for ((name, _), mut arg) in lambda.params.iter().zip(args) {
                    arg.name = name.clone();
                    out.names_mut().push(arg);
                }
                let result = self.block(out, &lambda.body, &lambda_env);
                out.names_mut().truncate(before);
                out.at = caller;
                result
            }
        };
        if *gives == Ty::Nothing {
            result.clear();
        }
        (gives.clone(), result)
    }

    /// The values of case `k` of the enum `ty`, holding the payload's
    /// values, if it has one: the case, those values, and then as many
    /// fillers as a larger case would take.
    fn case(
        &mut self,
        out: &mut Lowered<'a>,
        ty: &Ty,
        k: usize,
        payload: Option<&'a Expr>,
        env: &[Ty],
    ) -> Vec<Value> {
        let mut values = vec![Value::Case(k)];
        if let Some(p) = payload {
            values.extend(self.expr(out, p, env).1);
        }
        values.resize(self.width(ty), Value::Case(0));
        values
    }

    /// Lowers a `match` that gives `ty`: a decision between its blocks,
    /// each of which ends by copying what it gives, and the names defined
    /// before it that it assigns to, to the same locals. Gives the values
    /// of what it gives.
    ///
    /// While a function is lowered in place, a block may run a lambda that
    /// assigns a `var` of any frame, so every `var` is copied then.
    fn match_expr(
        &mut self,
        out: &mut Lowered<'a>,
        m: &'a Match,
        ty: &Ty,
        env: &[Ty],
    ) -> Vec<Value> {
        let (of_ty, of) = self.expr(out, &m.of, env);
        let results = self.locals(out, ty);
        let mut assigned = Vec::new();
        for arm in &m.arms {
            for stmt in statements(&arm.body) {
                let names = match stmt {
                    Stmt::Define { name, .. } | Stmt::Assign { name, .. } => vec![name.as_str()],
                    Stmt::Destructure { pattern, .. } => {
                        pattern_names(pattern).into_iter().map(|(n, _)| n).collect()
                    }
                    Stmt::Expr(_) => Vec::new(),
                };
                for name in names {
                    if let Some(at) = out.names().iter().position(|l| l.name == name)
                        && !assigned.contains(&(out.at, at))
                    {
                        assigned.push((out.at, at));
                    }
                }
            }
        }
        if out.frames.len() > 1 {
            for (f, frame) in out.frames.iter().enumerate() {
                for (at, local) in frame.iter().enumerate() {
                    if local.var && !assigned.contains(&(f, at)) {
                        assigned.push((f, at));
                    }
                }
            }
        }
        let merged = (assigned.into_iter())
            .map(|(f, at)| {
                let ty = out.frames[f][at].ty.clone();
                ((f, at), self.locals(out, &ty))
            })
            .collect();
        let arms = Arms {
            arms: &m.arms,
            env,
            results,
            merged,
        };
        let rows = (m.arms.iter().enumerate())
            .map(|(arm, a)| Row {
                arm,
                patterns: vec![Some(&a.pattern)],
                binds: Vec::new(),
            })
            .collect();
        let steps = self.decide(out, &arms, rows, &[(of_ty, of)]);
        out.steps.extend(steps);
        for ((f, at), locals) in &arms.merged {
            out.frames[*f][*at].values = locals.iter().map(|l| Value::Local(*l)).collect();
        }
        arms.results.iter().map(|l| Value::Local(*l)).collect()
    }

    /// The steps that run the block of the first of `rows` that matches the
    /// values in `columns`, each with its type, which the patterns of each
    /// row match in order.
    fn decide(
        &mut self,
        out: &mut Lowered<'a>,
        arms: &Arms<'a, '_>,
        rows: Vec<Row>,
        columns: &[(Ty, Vec<Value>)],
    ) -> Vec<Step> {
        let defs = self.typed.defs;
        // A bare name names what it matches, and then matches anything.
        let rows: Vec<Row> = (rows.into_iter())
            .map(|mut row| {
                for (pattern, (ty, value)) in row.patterns.iter_mut().zip(columns) {
                    if let Some(Pattern::Bind { name, .. }) = pattern {
                        row.binds.push((name.clone(), ty.clone(), value.clone()));
                        *pattern = None;
                    }
                }
                row
            })
            .collect();
        let Some(first) = rows.first() else {
            return Vec::new();
        };
        // The first value the first row takes apart.
        let Some(c) = first.patterns.iter().position(Option::is_some) else {
            return self.leaf(out, arms, first.clone());
        };
        // The rows and the columns with column `c` replaced by `inner`, and
        // the pattern of each row there by what `replace` gives for it, or
        // without the row if it gives `None`.
        let around = |inner: Vec<(Ty, Vec<Value>)>,
                      replace: &dyn Fn(Option<&Pattern>) -> Option<Patterns>| {
            let mut cols = columns[..c].to_vec();
            cols.extend(inner);
            cols.extend_from_slice(&columns[c + 1..]);
            let rows: Vec<Row> = (rows.iter())
                .filter_map(|r| {
                    let inner = replace(r.patterns[c])?;
                    let mut patterns = r.patterns[..c].to_vec();
                    patterns.extend(inner);
                    patterns.extend_from_slice(&r.patterns[c + 1..]);
                    Some(Row {
                        patterns,
                        ..r.clone()
                    })
                })
                .collect();
            (rows, cols)
        };
        let (ty, value) = &columns[c];
        match ty {
            Ty::Tuple(items) => {
                let inner = (items.iter().cloned())
                    .zip(self.elements(items, value))
                    .collect();
                let (rows, cols) = around(inner, &|p| {
                    Some(match p {
                        Some(Pattern::Tuple { items, .. }) => items.iter().map(Some).collect(),
                        _ => vec![None; items.len()],
                    })
                });
                self.decide(out, arms, rows, &cols)
            }
            Ty::Enum(e, args) => {
                let mut choices = Vec::new();
                for (k, case) in defs.file.enums[*e].cases.iter().enumerate() {
                    let holds = defs.cases[*e][k].as_ref().map(|t| t.subst(args));
                    let inner: Vec<(Ty, Vec<Value>)> = (holds.iter())
                        .map(|t| (t.clone(), value[1..1 + self.width(t)].to_vec()))
                        .collect();
                    let held = holds.is_some();
                    let (rows, cols) = around(inner, &|p| match p {
                        None => Some(if held { vec![None] } else { Vec::new() }),
                        Some(Pattern::Case { name, inner, .. }) if *name == case.name => {
                            Some(if held {
                                vec![inner.as_deref()]
                            } else {
                                Vec::new()
                            })
                        }
                        Some(_) => None,
                    });
                    choices.push(self.decide(out, arms, rows, &cols));
                }
                vec![Step::Match {
                    value: value[0],
                    arms: choices,
                }]
            }
            _ => unreachable!("checked"),
        }
    }

    /// The steps of the block a row leads to, with the names it binds, which
    /// end by copying what the block gives and the names it assigns to.
    fn leaf(&mut self, out: &mut Lowered<'a>, arms: &Arms<'a, '_>, row: Row) -> Vec<Step> {
        let before = std::mem::take(&mut out.steps);
        let frames = out.frames.clone();
        for (name, ty, value) in row.binds {
            out.define(&name, ty, value, false);
        }
        let gives = self.block(out, &arms.arms[row.arm].body, arms.env);
        for (local, value) in arms.results.iter().zip(gives) {
            out.steps.push(Step::Set {
                local: *local,
                value,
            });
        }
        for ((f, at), locals) in &arms.merged {
            for (local, value) in locals.iter().zip(out.frames[*f][*at].values.clone()) {
                out.steps.push(Step::Set {
                    local: *local,
                    value,
                });
            }
        }
        out.frames = frames;
        std::mem::replace(&mut out.steps, before)
    }

    /// Works out the fields in the order they are written, and then the
    /// defaults of those left out in the order the struct declares them,
    /// and gives their values in the order the struct declares them. `inner`
    /// stands for the struct's type parameters, which its defaults are
    /// written among.
    fn construction(
        &mut self,
        out: &mut Lowered<'a>,
        call: &'a Call,
        s: usize,
        env: &[Ty],
        inner: &[Ty],
    ) -> Vec<Value> {
        let defs = self.typed.defs;
        let def = &defs.file.structs[s];
        let mut fields: Vec<Vec<Value>> = vec![Vec::new(); def.fields.len()];
        for Arg { name, value, .. } in &call.args {
            let (_, value) = self.expr(out, value, env);
            let (name, _) = name.as_ref().expect("checked");
            let at = def.fields.iter().position(|f| &f.name == name);
            fields[at.expect("checked")] = value;
        }
        for (at, default) in defs.left_out(call) {
            fields[at] = self.expr(out, default, inner).1;
        }
        fields.concat()
    }
}
