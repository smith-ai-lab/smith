//! Checks a parsed program against the rules of the language and lowers it
//! to the scalar form.
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
//! many places as its largest case takes. The scalar form holds integers and
//! addresses alone.
//!
//! The type of each expression is recorded as it is checked, among the
//! type parameters of the definition it is written in, so lowering a copy
//! of a function reads the types of its expressions with its type
//! parameters replaced.
//!
//! A default is filled in where the call or construction that leaves it out
//! is written, so it is lowered as if it were written there, and no copy of
//! a function knows which of its arguments were left out.

use std::cell::{Cell, RefCell};
use std::collections::{HashMap, HashSet};
use std::sync::{Arc, OnceLock};

mod arith;
mod lists_and_text;
mod names;
mod numbers_and_operators;
mod ownership;
mod proof;
mod scope;
mod to_scalar;
mod traits;

use crate::diag::{Error, Pos};
use crate::hash::{Map, Set};
use crate::parser::outline::{Kind as PieceKind, Piece, outline};
use crate::parser::{
    Arg, ArithOp, Arm, Bound, Call, Enum, Expr, File, Form, Function, Generic, Impl, Lambda, Loop,
    Match, Over, Part, Pattern, Qualified, Range, Stmt, Struct, Trait, Type, Use,
};
use lists_and_text::{as_read, is_freeze, is_push, is_utf8};
use names::{own_final, renamed_tokens};
use numbers_and_operators::{Int, Narrowing, Numbers, flexible, literal_blocks, narrowing};
use traits::{
    Found, ImplInfo, PRELUDE_TRAITS, Requirement, TRAITS_NOT_BUILT, bounds_named, impl_info,
    needs_named, overlaps, split_needs, withouts,
};

include!(concat!(env!("OUT_DIR"), "/std_modules.rs"));

/// The effects a module of the standard library exports that its source does
/// not declare, because effect declarations are not built: the module, and
/// the effect. The compiler holds this fact until they are.
const LIBRARY_EFFECTS_EXPORTED: &[(&str, &str)] = &[("console", "console")];

/// The names that are in scope in every file with no `use`, as the page of
/// the prelude lists them, which a `use` is refused for.
const PRELUDE_LISTED: &[&str] = &[
    "Int",
    "Byte",
    "f16",
    "bf16",
    "f32",
    "f64",
    "bool",
    "str",
    "Nat",
    "List",
    "Slice",
    "Option",
    "Result",
    "Try",
    "then",
    "Order",
    "Eq",
    "Ord",
    "eq",
    "compare",
    "Add",
    "Sub",
    "Mul",
    "Div",
    "Rem",
    "Pow",
    "Neg",
    "add",
    "sub",
    "mul",
    "div",
    "rem",
    "pow",
    "neg",
    "Index",
    "valid",
    "at",
    "Iterate",
    "each",
    "Flow",
    "Exit",
    "exit",
    "Fmt",
    "Repr",
    "fmt",
    "repr",
    "Convert",
    "convert",
    "Hash",
    "Copy",
    "Discard",
    "FromInt",
    "FromFloat",
    "FromString",
    "FromArray",
    "alloc",
    "spawn",
    "gpu",
    "diverge",
    "foreign",
    "exec",
];

/// The effects the standard library declares, other than `console`.
const LIBRARY_EFFECTS: &[&str] = &["fs", "net", "time", "ui"];

/// The effects the compiler builds.
const BUILT: &[&str] = &["console", "alloc", "diverge"];

/// The effects built into the compiler.
const BUILT_IN_EFFECTS: &[&str] = &["alloc", "spawn", "gpu", "diverge", "foreign", "exec"];

/// The one type the prelude defines that the compiler builds so far.
const TEXT: &str = "str";

/// The types the language and its library define that the compiler does
/// not build yet.
const TYPES_NOT_BUILT: &[&str] = &[
    "usize", "f16", "bf16", "f32", "f64", "Pos", "Fin", "Ptr", "Map",
];

/// The names of sizes, which `std.bits` holds, and which the compiler knows
/// as the whole number types of that many bits.
const SIZES: &[&str] = &["u8", "u16", "u32", "u64", "i8", "i16", "i32", "i64"];

/// The items of the standard library that the compiler finds by name, as
/// `library_item` does, and so are read with their module.
const LEANED_ON: &[(&str, &str)] = &[
    ("std.deserialize", "ReadError"),
    ("std.deserialize", "Problem"),
];

/// The names of the types `PRELUDE` defines.
const PRELUDE_TYPES: &[&str] = &[
    TEXT,
    "Option",
    "Result",
    "bool",
    "Order",
    "Flow",
    "Utf8Error",
    "Int",
    "Byte",
    "i8",
    "i16",
    "i32",
    "i64",
    "u8",
    "u16",
    "u32",
    "u64",
];

/// Whether a definition is the prelude's.
fn of_prelude(pos: Pos) -> bool {
    pos.line == 0
}

include!(concat!(env!("OUT_DIR"), "/prelude.rs"));

/// An `impl` of the prelude, whole, with the names its header holds, which is
/// all of it that is read before a program reaches it.
pub(crate) struct PreludeImpl<'a> {
    source: &'a str,
    /// The trait it is for.
    name: &'a str,
    /// The names after the trait's: the types it is for, and its generics.
    types: &'a [&'a str],
}

/// What is read of the prelude's source, `std/prelude.smith`, which
/// `build.rs` divides into its items when the compiler is built. Every program
/// reads the types and traits, which the checker finds by name wherever syntax
/// means them, and an `impl` is read when the program reaches its trait or a
/// type it is for. The text is at line 0, which no file has, so nothing in a
/// file can be confused with it.
pub(crate) struct Prelude<'a> {
    declarations: &'a str,
    impls: &'a [PreludeImpl<'a>],
    /// The items read so far in the process, when the prelude is the one the
    /// compiler holds, so that a process that checks many programs reads each
    /// item once.
    parsed: Option<&'static Parsed>,
}

/// The prelude's types and traits, shared and not copied.
struct Declared {
    enums: Vec<Arc<Enum>>,
    structs: Vec<Arc<Struct>>,
    traits: Vec<Arc<Trait>>,
}

/// The items of the prelude's own, parsed once in a process.
struct Parsed {
    declarations: OnceLock<File>,
    impls: [OnceLock<Arc<Impl>>; PRELUDE_IMPLS.len()],
}

static PARSED: Parsed = Parsed {
    declarations: OnceLock::new(),
    impls: [const { OnceLock::new() }; PRELUDE_IMPLS.len()],
};

/// The prelude the compiler holds.
const PRELUDE: Prelude<'static> = Prelude {
    declarations: PRELUDE_DECLARATIONS,
    impls: PRELUDE_IMPLS,
    parsed: Some(&PARSED),
};

impl Prelude<'_> {
    /// The prelude's types and traits, which every program reads.
    fn declared(&self) -> Declared {
        let parse = || split_needs(prelude_file(self.declarations));
        let of = |items: &File| Declared {
            enums: items.enums.clone(),
            structs: items.structs.clone(),
            traits: items.traits.clone(),
        };
        match self.parsed {
            Some(parsed) => of(parsed.declarations.get_or_init(parse)),
            None => of(&parse()),
        }
    }

    /// The `impl` at the place in the prelude's source.
    fn impl_at(&self, i: usize) -> Arc<Impl> {
        let parse = || {
            let mut file = prelude_file(self.impls[i].source);
            file.impls.pop().expect("the prelude holds an impl here")
        };
        match self.parsed {
            Some(parsed) => parsed.impls[i].get_or_init(parse).clone(),
            None => parse(),
        }
    }

    /// The `impl`s of the prelude that the items `reached` in the file call
    /// for, which are those of a trait reached and those for a type reached,
    /// without the ones already `loaded`.
    fn wanted(&self, file: &File, reached: &[Item], loaded: &[usize]) -> Vec<usize> {
        let mut traits: Set<&str> = Set::default();
        let mut types: Set<&str> = Set::default();
        for item in reached {
            match *item {
                Item::Trait(k) => {
                    traits.insert(&file.traits[k].name);
                }
                Item::Struct(s) => {
                    types.insert(&file.structs[s].name);
                }
                Item::Enum(e) => {
                    types.insert(&file.enums[e].name);
                }
                Item::Fn(_) | Item::Impl(_) => {}
            }
        }
        (self.impls.iter().enumerate())
            .filter(|(i, im)| {
                !loaded.contains(i)
                    && (traits.contains(im.name) || im.types.iter().any(|t| types.contains(t)))
            })
            .map(|(i, _)| i)
            .collect()
    }
}

/// The definitions in the source of the prelude, at line 0, which no file has.
fn prelude_file(source: &str) -> File {
    let mut tokens = crate::lexer::lex(source.as_bytes()).expect("the prelude lexes");
    for t in &mut tokens {
        t.pos.line = 0;
    }
    crate::parser::parse(&tokens).expect("the prelude parses")
}

/// The file with the prelude's types and traits before its own, and the
/// `impl`s of the prelude at the places `loaded` gives.
fn with_prelude(mut file: File, prelude: &Prelude, loaded: &[usize]) -> File {
    let declared = prelude.declared();
    let start = prelude_file(&start_function(&file));
    file.enums.splice(0..0, declared.enums);
    file.structs.splice(0..0, declared.structs);
    file.traits.splice(0..0, declared.traits);
    file.impls
        .splice(0..0, loaded.iter().map(|&i| prelude.impl_at(i)));
    file.functions.splice(0..0, start.functions);
    file
}

/// The name of the function the program starts at when `main` gives a
/// status.
const START: &str = "__main";

/// The function that starts a program whose `main` returns a type: it calls
/// `main` and ends the program with the status that the `Exit` of what
/// `main` returns gives. It lists the effects of `main`, and the two that
/// `Exit` carries. A program whose `main` returns nothing starts at `main`.
fn start_function(file: &File) -> String {
    let Some(main) = file
        .functions
        .iter()
        .find(|f| f.name == "main" && f.result.is_some())
    else {
        return String::new();
    };
    let mut effects: Vec<&str> = vec!["console", "alloc"];
    for (name, _) in &main.effects {
        if !effects.contains(&name.as_str()) {
            effects.push(name);
        }
    }
    let (params, args) = if main.params.is_empty() {
        ("", "")
    } else {
        ("args: List[str]", "args")
    };
    format!(
        "\nfn {START}({params}) ! {} {{\n  __end(Exit.exit(main({args})))\n}}\n",
        effects.join(", ")
    )
}

/// What an expression gives.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
enum Ty {
    Text,
    /// A whole number type.
    Int(Int),
    /// A struct, by its place in the file's structs, with a type for each
    /// of its type parameters.
    Struct(usize, Vec<Ty>),
    /// An enum, by its place in the file's enums, with a type for each of
    /// its type parameters.
    Enum(usize, Vec<Ty>),
    /// A tuple of two values or more, with the type of each.
    Tuple(Vec<Ty>),
    /// A `List`, which holds its one element type.
    List(Vec<Ty>),
    /// A `Slice`, a run of elements that cannot change, which holds its one
    /// element type.
    Slice(Vec<Ty>),
    /// A function: the types of its parameters, followed by the type it
    /// gives.
    Fn(Vec<Ty>),
    /// A type parameter of the function, the implementation, the struct,
    /// or the trait the type is written in, by its place among them.
    Param(usize),
    /// The empty tuple, `()`, which a function with no return type gives.
    Nothing,
    /// A whole number bound to a name, whose type the uses of the name have
    /// not yet decided, by its place among those of the function. It exists
    /// only while the uses are looked at, before the body is checked.
    Flex(usize),
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

    /// The list of this element type, or unknown if it is.
    fn list(element: Ty) -> Ty {
        if element.unknown() {
            Ty::Unknown
        } else {
            Ty::List(vec![element])
        }
    }

    /// The slice of this element type, or unknown if it is.
    fn slice(element: Ty) -> Ty {
        if element.unknown() {
            Ty::Unknown
        } else {
            Ty::Slice(vec![element])
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
            Ty::Struct(_, args)
            | Ty::Enum(_, args)
            | Ty::Tuple(args)
            | Ty::Fn(args)
            | Ty::List(args)
            | Ty::Slice(args) => args,
            _ => &[],
        }
    }

    /// The type with each undecided whole number made an `i64`, which is
    /// what it is when nothing decides it.
    fn concrete(&self) -> Ty {
        match self {
            Ty::Flex(_) => Ty::Int(Int::I64),
            t => t.with_args(t.args().iter().map(Ty::concrete).collect()),
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
            Ty::List(_) => Ty::list(args.into_iter().next().expect("an element type")),
            Ty::Slice(_) => Ty::slice(args.into_iter().next().expect("an element type")),
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
            Ty::List(of) => Ty::List(of.iter().map(|t| t.partial(args)).collect()),
            Ty::Slice(of) => Ty::Slice(of.iter().map(|t| t.partial(args)).collect()),
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
        (Ty::Tuple(x), Ty::Tuple(y))
        | (Ty::Fn(x), Ty::Fn(y))
        | (Ty::List(x), Ty::List(y))
        | (Ty::Slice(x), Ty::Slice(y)) => {
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
        (Ty::Param(i), _) if *i >= sol.len() => true,
        (Ty::Param(i), given) => match &sol[*i] {
            None => {
                sol[*i] = Some(given.clone());
                true
            }
            Some(t) if *t == Ty::Unknown => true,
            Some(t) if t == given => true,
            Some(t) => {
                let agreed = flex_pair(t, given);
                if agreed && matches!(t, Ty::Flex(_)) {
                    sol[*i] = Some(given.clone());
                }
                agreed
            }
        },
        (a, b) if flex_pair(a, b) => true,
        (Ty::Struct(s, a), Ty::Struct(t, b)) | (Ty::Enum(s, a), Ty::Enum(t, b)) => {
            s == t && a.iter().zip(b).all(|(a, b)| unify(a, b, sol))
        }
        (Ty::Tuple(a), Ty::Tuple(b))
        | (Ty::Fn(a), Ty::Fn(b))
        | (Ty::List(a), Ty::List(b))
        | (Ty::Slice(a), Ty::Slice(b)) => {
            a.len() == b.len() && a.iter().zip(b).all(|(a, b)| unify(a, b, sol))
        }
        (a, b) => a == b,
    }
}

/// What the uses of the whole numbers bound to names, whose types are not
/// yet decided, have asked of them while a body is looked at.
#[derive(Default)]
struct Uses {
    /// The place of the use being looked at.
    at: Option<Pos>,
    /// The type a use asked of a number, and where.
    wants: Vec<(usize, Int, Pos)>,
    /// Two numbers that must be of one type.
    same: Vec<(usize, usize)>,
}

thread_local! {
    static USES: std::cell::RefCell<Uses> = std::cell::RefCell::new(Uses::default());
}

/// Notes the place of the use that is looked at next.
fn use_at(pos: Pos) {
    USES.with(|u| u.borrow_mut().at = Some(pos));
}

/// Whether one of two types is an undecided whole number and the other asks
/// of it a whole number type or another undecided one, which is noted.
fn flex_pair(a: &Ty, b: &Ty) -> bool {
    USES.with(|u| {
        let mut u = u.borrow_mut();
        let at = u.at.unwrap_or(Pos::START);
        match (a, b) {
            (Ty::Flex(k), Ty::Int(t)) | (Ty::Int(t), Ty::Flex(k)) => {
                u.wants.push((*k, *t, at));
                true
            }
            (Ty::Flex(j), Ty::Flex(k)) => {
                u.same.push((*j, *k));
                true
            }
            _ => false,
        }
    })
}

/// The type parameters a type is written among: their names, and what the
/// definition requires of them, as `T: Trait` and `requires Trait[T]` do.
#[derive(Clone, Debug, Default)]
struct Scope {
    names: Vec<String>,
    requires: Vec<Requirement>,
    /// Whether a requirement of each type parameter was reported as a
    /// mistake. Whatever it would allow is allowed, so nothing it explains
    /// is reported.
    invalid: Vec<bool>,
    /// Whether a bound or a `requires` line says that each type parameter has
    /// `Copy`, and the same for `Discard`. Nothing else is known of one.
    copy: Vec<bool>,
    discard: Vec<bool>,
}

/// Whether `expr` is a whole number literal that an `i64` holds.
fn fits_i64(expr: &Expr) -> bool {
    match expr {
        Expr::Number {
            magnitude,
            negative,
            ..
        } if *negative => *magnitude <= Int::I64.min().unsigned_abs(),
        Expr::Number { magnitude, .. } => *magnitude <= Int::I64.max() as u128,
        _ => false,
    }
}

/// Fills in `sol` the type parameters of `pattern` that `given` says, where
/// `given` is wholly known and holds no whole number whose type is not yet
/// decided. Nothing is noted of a use, and a mismatch is left to the call.
fn seed(pattern: &Ty, given: &Ty, sol: &mut [Option<Ty>]) {
    match (pattern, given) {
        (Ty::Param(i), given) if sol[*i].is_none() && !given.unknown() && !has_flex(given) => {
            sol[*i] = Some(given.clone());
        }
        (Ty::Param(_), _) => {}
        (Ty::Struct(s, a), Ty::Struct(t, b)) | (Ty::Enum(s, a), Ty::Enum(t, b)) if s == t => {
            a.iter().zip(b).for_each(|(a, b)| seed(a, b, sol));
        }
        (Ty::Tuple(a), Ty::Tuple(b)) | (Ty::List(a), Ty::List(b)) if a.len() == b.len() => {
            a.iter().zip(b).for_each(|(a, b)| seed(a, b, sol));
        }
        _ => {}
    }
}

/// Whether a whole number whose type is not yet decided is part of `t`.
fn has_flex(t: &Ty) -> bool {
    matches!(t, Ty::Flex(_)) || t.args().iter().any(has_flex)
}

/// What a caller needs to know of a function.
struct Signature {
    /// The type parameters its types are written among.
    scope: Scope,
    params: Vec<(String, Ty)>,
    /// Whether each parameter has a default, so that a call may leave it
    /// out.
    defaults: Vec<bool>,
    /// The type each type parameter takes when the arguments of a call do
    /// not settle it, among the type parameters, for those that have a
    /// default.
    param_defaults: Vec<Option<Ty>>,
    gives: Ty,
    /// The effects it carries that a caller must have, which are those it
    /// lists that the compiler builds.
    effects: Vec<String>,
}

impl Signature {
    /// What `__end` takes: the status, a byte.
    fn end() -> Signature {
        Signature {
            scope: Scope::default(),
            params: vec![("status".to_string(), Ty::Int(Int::U8))],
            defaults: vec![false],
            param_defaults: Vec::new(),
            gives: Ty::Nothing,
            effects: Vec::new(),
        }
    }

    fn print() -> Signature {
        Signature {
            scope: Scope::default(),
            params: vec![("text".to_string(), Ty::Text)],
            defaults: vec![false],
            param_defaults: Vec::new(),
            gives: Ty::Nothing,
            effects: vec!["console".to_string()],
        }
    }
}

/// The definitions of a file, by name, and what is known of each before
/// any body is checked.
struct Defs<'a> {
    file: &'a File,
    function_names: Map<&'a str, usize>,
    struct_names: Map<&'a str, usize>,
    enum_names: Map<&'a str, usize>,
    trait_names: Map<&'a str, usize>,
    /// The functions of traits with each name, as the trait and the
    /// function's place in it.
    trait_fns: Map<&'a str, Vec<(usize, usize)>>,
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
    /// The prelude's `Fmt`, `Repr`, and `Eq`, which a type is given
    /// automatically, by their places in the file's traits.
    fmt: usize,
    repr: usize,
    eq: usize,
    /// The prelude's `Exit` and `Iterate`, by their places in the file's
    /// traits.
    exit: usize,
    iterate: usize,
    /// The prelude's `Ord`, `Hash`, `Convert`, and `Bits`, by their places in
    /// the file's traits.
    ord: usize,
    hash: usize,
    convert: usize,
    bits: usize,
    /// The standard library's `Serialize` and `Writer`, which `std.serialize`
    /// defines, and its `Deserialize` and `Reader`, which `std.deserialize`
    /// defines, by their places in the file's traits, when the program has
    /// that module.
    serialize: Option<usize>,
    writer: Option<usize>,
    deserialize: Option<usize>,
    reader: Option<usize>,
    /// The `ReadError` and the `Problem` of `std.deserialize`, by their places
    /// in the file's structs and enums, when the program has that module.
    read_error: Option<usize>,
    problem: Option<usize>,
    /// The prelude's `bool`, `Order`, `Option`, `Flow`, and `Result`, by their
    /// places in the file's enums.
    boolean: usize,
    order: usize,
    option: usize,
    flow: usize,
    result: usize,
    /// Each trait taken away from a struct or an enum by `without`.
    withouts: Vec<(usize, Def)>,
    /// How many defaults of type parameters are being resolved inside one
    /// another, so that a default that names its own type ends.
    default_depth: Cell<u8>,
    /// How the program's own code wrote `i64` and `u8`, so that an error
    /// names a type as the program wrote it.
    spelled: Cell<Spelled>,
    /// The names and types of the parameters of the function whose signature
    /// is being read, which a bound of a range may be.
    bound_params: RefCell<Vec<(String, Type)>>,
}

/// For each whole number type that has two names, whether the first name the
/// program wrote for it is the new one (`Int`, `Byte`), once it wrote one.
#[derive(Clone, Copy, Default)]
struct Spelled {
    int: Option<bool>,
    byte: Option<bool>,
    nat: Option<bool>,
}

impl<'a> Defs<'a> {
    /// The number a bound of the range type `t` stands for where only the
    /// range of the bound is known: a number is itself, and a name is the
    /// least number of its parameter if it is the first bound of the range
    /// and the greatest if it is the end.
    fn bound_value(
        &self,
        bound: &Bound,
        first: bool,
        t: &Type,
        scope: &Scope,
        errors: &mut Vec<Error>,
    ) -> Option<i128> {
        let name = match bound {
            Bound::Number(n) => return Some(*n),
            Bound::Name(name) => name,
        };
        let params = self.bound_params.take();
        let found = params
            .iter()
            .find(|(n, _)| n == name)
            .map(|(_, t)| t.clone());
        let ty = found.map(|p| self.resolve(&p, scope, errors));
        *self.bound_params.borrow_mut() = params;
        match ty {
            Some(Ty::Int(int)) => Some(if first { int.min() } else { int.max() }),
            Some(Ty::Unknown) | None => None,
            Some(_) => {
                errors.push(Error::new(
                    t.pos,
                    format!("`{name}` is not a whole number, and a bound is one"),
                ));
                None
            }
        }
    }

    /// The type a type written in the source stands for, among the type
    /// parameters of `scope`. Mistakes go in `errors`.
    fn resolve(&self, t: &Type, scope: &Scope, errors: &mut Vec<Error>) -> Ty {
        let name = t.name.as_str();
        if t.unbuilt() {
            return Ty::Unknown;
        }
        if let Some((first, end)) = &t.range {
            let (Some(min), Some(end)) = (
                self.bound_value(first, true, t, scope, errors),
                self.bound_value(end, false, t, scope, errors),
            ) else {
                return Ty::Unknown;
            };
            // A range that no number is in is of the least bound.
            return match Int::between(min, (end - 1).max(min - 1)) {
                Some(int) => Ty::Int(int),
                None => {
                    errors.push(Error::new(
                        t.pos,
                        format!(
                            "`{}` needs more than 64 bits, which is not built yet",
                            t.written()
                        ),
                    ));
                    Ty::Unknown
                }
            };
        }
        if name.is_empty() {
            let items: Vec<Ty> = (t.args.iter())
                .map(|a| self.resolve(a, scope, errors))
                .collect();
            return if t.function {
                Ty::func(items)
            } else {
                Ty::tuple(items)
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
            let Some(args) =
                self.arguments_of(&self.file.structs[s].generics, args, name, t, errors)
            else {
                return Ty::Unknown;
            };
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
            let Some(args) = self.arguments_of(&self.file.enums[e].generics, args, name, t, errors)
            else {
                return Ty::Unknown;
            };
            return if self.cyclic_enums[e] {
                Ty::Unknown
            } else {
                Ty::enumeration(e, args)
            };
        }
        if name == "List" {
            if t.args.len() != 1 {
                errors.push(Error::new(
                    t.pos,
                    format!(
                        "`List` takes one type argument, and this gives {}",
                        t.args.len()
                    ),
                ));
                return Ty::Unknown;
            }
            return match self.resolve(&t.args[0], scope, errors) {
                Ty::Unknown => Ty::Unknown,
                Ty::Fn(_) | Ty::Nothing => {
                    errors.push(Error::unsupported(t.pos, "a list of functions or of `()`"));
                    Ty::Unknown
                }
                element => Ty::list(element),
            };
        }
        if name == "Slice" && !self.struct_names.contains_key(name) {
            if t.args.len() != 1 {
                errors.push(Error::new(
                    t.pos,
                    format!(
                        "`Slice` takes one type argument, and this gives {}",
                        t.args.len()
                    ),
                ));
                return Ty::Unknown;
            }
            return match self.resolve(&t.args[0], scope, errors) {
                Ty::Unknown => Ty::Unknown,
                Ty::Fn(_) | Ty::Nothing => {
                    errors.push(Error::unsupported(t.pos, "a slice of functions or of `()`"));
                    Ty::Unknown
                }
                element => Ty::slice(element),
            };
        }
        if name == TEXT || Int::named(name).is_some() {
            no_args("the type", errors);
            if !of_prelude(t.pos) && !of_std(t.pos) {
                self.spell(name);
            }
            return match (t.args.is_empty(), Int::named(name)) {
                (false, _) => Ty::Unknown,
                (_, Some(int)) => Ty::Int(int),
                (_, None) => Ty::Text,
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

    /// The prelude's `bool`.
    fn bool_ty(&self) -> Ty {
        Ty::Enum(self.boolean, Vec::new())
    }

    /// Whether the type is the prelude's `Result`, whose failure must not be
    /// dropped.
    fn is_result(&self, ty: &Ty) -> bool {
        match ty {
            Ty::Enum(e, _) => *e == self.result,
            _ => false,
        }
    }

    /// The types of a type's parameters, given the arguments written for
    /// it: the defaults of the parameters left out are the rest. A count
    /// the type does not take is reported, and gives `None`.
    fn arguments_of(
        &self,
        generics: &[Generic],
        mut args: Vec<Ty>,
        name: &str,
        t: &Type,
        errors: &mut Vec<Error>,
    ) -> Option<Vec<Ty>> {
        let wanted = generics.len();
        let needed = generics.iter().filter(|g| g.default.is_none()).count();
        if args.len() < needed || args.len() > wanted {
            let takes = if needed == wanted {
                count(wanted, "type argument")
            } else {
                format!("{needed} to {}", count(wanted, "type argument"))
            };
            errors.push(Error::new(
                t.pos,
                format!("`{name}` takes {takes}, and this gives {}", args.len()),
            ));
            return None;
        }
        self.fill_defaults(generics, &mut args);
        Some(args)
    }

    /// Adds the type of each parameter of `generics` that `args` does not
    /// reach, from its default, with the parameters before it as `args` has
    /// them.
    fn fill_defaults(&self, generics: &[Generic], args: &mut Vec<Ty>) {
        if args.len() >= generics.len() {
            return;
        }
        let depth = self.default_depth.get();
        let scope = self.scope(generics, &[]);
        for g in &generics[args.len()..] {
            let ty = match &g.default {
                Some(d) if depth < 8 => {
                    self.default_depth.set(depth + 1);
                    let ty = self.quietly(d, &scope);
                    self.default_depth.set(depth);
                    let mut known = args.clone();
                    known.resize(generics.len(), Ty::Unknown);
                    ty.subst(&known)
                }
                _ => Ty::Unknown,
            };
            args.push(ty);
        }
    }

    /// Notes how the program wrote a whole number type, when it is one of the
    /// two that have two names and the program has not written either yet.
    fn spell(&self, name: &str) {
        let mut spelled = self.spelled.get();
        match name {
            "Int" | "i64" => spelled.int.get_or_insert(name == "Int"),
            "Byte" | "u8" => spelled.byte.get_or_insert(name == "Byte"),
            "Nat" | "u64" => spelled.nat.get_or_insert(name == "Nat"),
            _ => return,
        };
        self.spelled.set(spelled);
    }

    /// How a whole number type is written in an error: `Int` and `Byte` where
    /// the program wrote those names first, and the name of the type
    /// otherwise.
    fn spelling(&self, int: Int) -> String {
        let spelled = self.spelled.get();
        match int {
            Int::I64 if spelled.int == Some(true) => "Int".to_string(),
            Int::U8 if spelled.byte == Some(true) => "Byte".to_string(),
            Int::U64 if spelled.nat == Some(true) => "Nat".to_string(),
            _ => int.name(),
        }
    }

    /// The type, with nothing reported.
    fn quietly(&self, t: &Type, scope: &Scope) -> Ty {
        self.resolve(t, scope, &mut Vec::new())
    }

    /// Whether the name is that of a type, other than a type parameter.
    fn is_type(&self, name: &str) -> bool {
        self.struct_names.contains_key(name)
            || self.enum_names.contains_key(name)
            || name == TEXT
            || name == "Slice"
            || Int::named(name).is_some()
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

    /// A function's signature among the type parameters of `scope`.
    fn signature(&self, f: &Function, scope: Scope) -> Signature {
        *self.bound_params.borrow_mut() = (f.params.iter())
            .map(|p| (p.name.clone(), p.ty.clone()))
            .collect();
        let signature = self.signature_read(f, scope);
        self.bound_params.borrow_mut().clear();
        signature
    }

    fn signature_read(&self, f: &Function, scope: Scope) -> Signature {
        Signature {
            params: (f.params.iter())
                .map(|p| (p.name.clone(), self.quietly(&p.ty, &scope)))
                .collect(),
            defaults: f.params.iter().map(|p| p.default.is_some()).collect(),
            param_defaults: (f.generics.iter())
                .map(|g| g.default.as_ref().map(|d| self.quietly(d, &scope)))
                .collect(),
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
            Ty::Int(int) => self.spelling(*int).to_string(),
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
            Ty::List(items) => format!("List[{}]", self.text(&items[0], scope)),
            Ty::Slice(items) => format!("Slice[{}]", self.text(&items[0], scope)),
            Ty::Fn(items) => {
                let items: Vec<String> = items.iter().map(|t| self.text(t, scope)).collect();
                function_text(&items)
            }
            Ty::Param(p) => scope.names[*p].clone(),
            Ty::Nothing => "()".to_string(),
            Ty::Unknown => "nothing".to_string(),
            Ty::Flex(_) => "number".to_string(),
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
                    function_text(&items)
                }
                Ty::Struct(_, args) | Ty::Enum(_, args) if !args.is_empty() => {
                    let args: Vec<String> = args
                        .iter()
                        .map(|a| text(defs, a, sol, vars, scope))
                        .collect();
                    format!("{}[{}]", defs.type_name(t), args.join(", "))
                }
                Ty::List(args) => format!("List[{}]", text(defs, &args[0], sol, vars, scope)),
                Ty::Slice(args) => format!("Slice[{}]", text(defs, &args[0], sol, vars, scope)),
                t => defs.text(t, scope),
            }
        }
        match pattern.solved(sol) {
            Ty::Text => "text".to_string(),
            Ty::Nothing => "nothing".to_string(),
            _ => format!("a `{}`", text(self, pattern, sol, vars, scope)),
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
    sentence(list.iter().map(|n| format!("`{n}`")).collect())
}

/// How a parameter is spoken of in an error: by its name, in backticks, or
/// by its place when it is written as a pattern and has no name of its own,
/// as the parser gives it one that holds an `@`.
fn param_words(name: &str, place: usize) -> String {
    const ORDINALS: [&str; 10] = [
        "first", "second", "third", "fourth", "fifth", "sixth", "seventh", "eighth", "ninth",
        "tenth",
    ];
    if !name.contains('@') {
        return format!("`{name}`");
    }
    match ORDINALS.get(place) {
        Some(ordinal) => format!("the {ordinal} parameter"),
        None => format!("parameter {}", place + 1),
    }
}

/// Parts of a sentence, joined as a sentence joins them.
fn sentence(quoted: Vec<String>) -> String {
    match quoted.as_slice() {
        [] => String::new(),
        [one] => one.clone(),
        [a, b] => format!("{a} and {b}"),
        [rest @ .., last] => format!("{}, and {last}", rest.join(", ")),
    }
}

/// A function type written from the text of its parameters and its result,
/// the result last: the result is left out when it is `()`.
fn function_text(items: &[String]) -> String {
    let (gives, params) = items.split_last().expect("a result");
    if gives == "()" {
        format!("fn({})", params.join(", "))
    } else {
        format!("fn({}) -> {gives}", params.join(", "))
    }
}

/// How a type is written in the source.
fn written_type(t: &Type) -> String {
    t.written()
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
    written_with(&|_| None, e)
}

/// How `e` is written, where a name that `s` gives text for is written as it.
fn written_with(s: &dyn Fn(&str) -> Option<String>, e: &Expr) -> String {
    match e {
        Expr::Number { text, negative, .. } => {
            format!("{}{text}", if *negative { "-" } else { "" })
        }
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
                        let quoted = written_with(
                            s,
                            &Expr::Text {
                                text: text.clone(),
                                pos: Pos::START,
                            },
                        );
                        out.push_str(&quoted[1..quoted.len() - 1]);
                    }
                    Part::Expr(e) => {
                        let Expr::Call(call) = e else {
                            unreachable!("the call of `Fmt.fmt`")
                        };
                        out.push_str(&format!("{{{}}}", written_with(s, &call.args[0].value)));
                    }
                }
            }
            out.push('"');
            out
        }
        Expr::Name { name, .. } => s(name).unwrap_or_else(|| name.clone()),
        Expr::Call(call) => {
            let args: Vec<String> = (call.args.iter())
                .filter(|a| !a.after)
                .map(|a| match &a.name {
                    Some((name, _)) => format!("{name}: {}", written_with(s, &a.value)),
                    None => written_with(s, &a.value),
                })
                .collect();
            let mut text = match &call.qualifier {
                Some((q, _)) => format!("{q}.{}({})", call.callee, args.join(", ")),
                None => format!("{}({})", call.callee, args.join(", ")),
            };
            for a in call.args.iter().filter(|a| a.after) {
                text.push_str(&format!(" {}", written_with(s, &a.value)));
            }
            text
        }
        Expr::Field { of, name, .. } => format!("{}.{name}", written_with(s, of)),
        Expr::Arith {
            op, left, right, ..
        } => {
            // An operand that groups looser than the operator, and a right
            // operand that groups as tightly, was in parentheses.
            let binds = |op: &ArithOp| matches!(op, ArithOp::Mul) as u8;
            let side = |e: &Expr, right: bool| match e {
                Expr::Arith { op: inner, .. }
                    if binds(inner) < binds(op) || (right && binds(inner) == binds(op)) =>
                {
                    format!("({})", written_with(s, e))
                }
                _ => written_with(s, e),
            };
            format!(
                "{} {} {}",
                side(left, false),
                op.symbol(),
                side(right, true)
            )
        }
        Expr::Case { name, payload, .. } => match payload {
            Some(p) => format!(".{name}({})", written_with(s, p)),
            None => format!(".{name}"),
        },
        Expr::Match(m) => {
            // The side of `and` or `or` that runs only sometimes is what one
            // of the blocks gives.
            let side = |arm: usize| match m.arms[arm].body.as_slice() {
                [Stmt::Expr(e)] => written_with(s, e),
                _ => "...".to_string(),
            };
            match m.form {
                Form::Match => format!("match {} {{ ... }}", written_with(s, &m.of)),
                Form::If { .. } => format!("if {} {{ ... }}", written_with(s, &m.of)),
                Form::Not => format!("not ({})", written_with(s, &m.of)),
                Form::And => format!("({}) and ({})", written_with(s, &m.of), side(0)),
                Form::Or => format!("({}) or ({})", written_with(s, &m.of), side(1)),
                Form::Try => match &m.of {
                    Expr::Call(split) => format!("try {}", written_with(s, &split.args[0].value)),
                    of => format!("try {}", written_with(s, of)),
                },
            }
        }
        Expr::Tuple { items, .. } => {
            let items: Vec<String> = items.iter().map(written_expr).collect();
            format!("({})", items.join(", "))
        }
        Expr::List { items, .. } => {
            let items: Vec<String> = items.iter().map(|i| written_with(s, i)).collect();
            format!("[{}]", items.join(", "))
        }
        Expr::Index { of, index, .. } => {
            format!("{}[{}]", written_with(s, of), written_with(s, index))
        }
        Expr::Slice { of, from, to, .. } => {
            let end = |e: &Option<Box<Expr>>| e.as_ref().map(|e| written_with(s, e));
            format!(
                "{}[{}..{}]",
                written_with(s, of),
                end(from).unwrap_or_default(),
                end(to).unwrap_or_default()
            )
        }
        Expr::Lambda(l) if l.params.is_empty() => "{ ... }".to_string(),
        Expr::Lambda(l) => {
            let params: Vec<&str> = l.params.iter().map(|(p, _)| p.as_str()).collect();
            format!("fn({}) {{ ... }}", params.join(", "))
        }
        Expr::Loop(l) => match (&l.range, &l.over) {
            (Some(r), _) => format!(
                "loop {} in {}..{} {{ ... }}",
                r.name,
                written_with(s, &r.from),
                written_with(s, &r.to)
            ),
            (None, Some(o)) => {
                format!("loop {} in {} {{ ... }}", o.name, written_with(s, &o.value))
            }
            (None, None) => match &l.decreases {
                Some(e) => format!("loop decreases {} {{ ... }}", written_with(s, e)),
                None => "loop { ... }".to_string(),
            },
        },
        Expr::Break { .. } => "break".to_string(),
        Expr::Unbuilt { .. } => "what is not built".to_string(),
        Expr::Return { value: None, .. } => "return".to_string(),
        Expr::Return { value: Some(v), .. } => format!("return {}", written_with(s, v)),
    }
}

/// A definition of the file, by its place among those of its kind.
// Equality is the derived one, with a count of its calls, so it agrees with `Hash`.
#[allow(clippy::derived_hash_with_manual_eq)]
#[derive(Clone, Copy, Debug, Eq, Hash)]
enum Item {
    Fn(usize),
    Struct(usize),
    Enum(usize),
    Trait(usize),
    Impl(usize),
}

impl PartialEq for Item {
    fn eq(&self, other: &Item) -> bool {
        crate::work::item();
        std::mem::discriminant(self) == std::mem::discriminant(other)
            && match (self, other) {
                (Item::Fn(a), Item::Fn(b))
                | (Item::Struct(a), Item::Struct(b))
                | (Item::Enum(a), Item::Enum(b))
                | (Item::Trait(a), Item::Trait(b))
                | (Item::Impl(a), Item::Impl(b)) => a == b,
                _ => false,
            }
    }
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
    pub program: crate::scalar::Program,
    pub warnings: Vec<Error>,
    /// How many functions and implementations of the prelude the program
    /// reaches, which are the ones the checker checks.
    pub prelude_reached: (usize, usize),
    /// How many `impl`s of the prelude were read for the program.
    pub prelude_loaded: usize,
}

/// Checks the file, and lowers what `main` reaches to the scalar form. Every
/// error is reported, except one that a mistake already reported would
/// explain.
pub fn check(file: File, library: &Library) -> Result<Checked, Vec<Error>> {
    check_with(file, library, &PRELUDE)
}

/// `check`, with the text of the prelude given, which is divided now.
pub(crate) fn check_with_source(
    file: File,
    library: &Library,
    prelude: &str,
) -> Result<Checked, Vec<Error>> {
    let divided = crate::divide::divide(prelude);
    let impls: Vec<PreludeImpl> = (divided.impls.iter())
        .map(|im| PreludeImpl {
            source: im.source,
            name: im.name,
            types: &im.types,
        })
        .collect();
    let prelude = Prelude {
        declarations: &divided.declarations,
        impls: &impls,
        parsed: None,
    };
    check_with(file, library, &prelude)
}

/// `check`, with the prelude given.
fn check_with(file: File, library: &Library, prelude: &Prelude) -> Result<Checked, Vec<Error>> {
    check_reading(file, library, prelude, None)
}

/// `check_with`, in which `whole` names a module of the standard library that
/// is read whole, and everything in the library is reached.
fn check_reading(
    mut file: File,
    library: &Library,
    prelude: &Prelude,
    whole: Option<&str>,
) -> Result<Checked, Vec<Error>> {
    let mut resolver = Resolver::new(library);
    resolver.whole = whole.is_some();
    let defined: HashSet<String> = defined_names(&file).into_iter().map(String::from).collect();
    resolver.resolve(
        Reader::Entry,
        &file.uses,
        &defined,
        &file.written,
        &file.qualified,
        &scope::used_names(&file),
    );
    if let Some(module) = whole {
        resolver.module(module, Pos::START);
    }
    let whole = whole.is_some();
    let Resolver {
        read,
        labels,
        mut errors,
        warnings,
        refused,
        unimported,
        ..
    } = resolver;
    // What the file uses that is not built is reported with its other
    // errors, as the parser read past it.
    errors.append(&mut file.unbuilt);
    placement(&file, &mut errors);
    for module in &read {
        placement(&module.file, &mut errors);
    }
    // The names the file writes, which name what it takes from the modules.
    let mut written: Vec<String> = std::mem::take(&mut file.idents)
        .into_iter()
        .map(|(name, _)| name)
        .collect();
    written.extend((file.qualified.iter()).map(|q| format!("{}@{}", q.name.0, q.module)));
    let (mut file, items, mut lazy) = splice(file, read, &mut errors);
    lazy.written = written;
    file.modules = module_names(&labels, library);
    let checked = check_file(file, items, lazy, errors, warnings, prelude, whole);
    if unimported.is_empty() {
        return checked
            .map(|mut checked| {
                checked.warnings = localize(checked.warnings, &labels);
                checked
            })
            .map_err(|errors| {
                let errors = errors.into_iter().filter(|e| !refused.contains(&e.pos));
                localize(errors.collect(), &labels)
            });
    }
    let mut errors: Vec<Error> = (checked.err().into_iter().flatten())
        .filter(|e| !refused.contains(&e.pos))
        .chain(unimported)
        .collect();
    errors.sort_by_key(|e| e.pos);
    Err(localize(errors, &labels))
}

/// The errors in every module of the standard library, each read whole and
/// checked whole: every function, type, trait, and `impl` in it, whether or not
/// a program reaches it, as no program is held to reach what it does not.
pub fn check_standard_library() -> Vec<Error> {
    check_library(&Library::standard())
}

/// The errors in every module of the library, each read whole and checked
/// whole.
pub fn check_library(library: &Library) -> Vec<Error> {
    let mut found = Vec::new();
    for (name, _) in &library.modules {
        let tokens = crate::lexer::lex(b"fn main() {}\n").expect("the file lexes");
        let file = crate::parser::parse(&tokens).expect("the file parses");
        if let Err(errors) = check_reading(file, library, &PRELUDE, Some(&format!("std.{name}"))) {
            found.extend(errors);
        }
    }
    found
}

/// The errors, or else the warnings, found in reading a module and the
/// modules it imports: what its syntax and its `use` lines say, and where its
/// `impl`s are. Nothing is built, so what no `main` reaches is not checked.
pub fn check_module(name: &str, library: &Library) -> Result<Vec<Error>, Vec<Error>> {
    let mut resolver = Resolver::new(library);
    resolver.whole = true;
    resolver.module(name, Pos::START);
    let Resolver {
        read,
        labels,
        mut errors,
        warnings,
        ..
    } = resolver;
    for module in &read {
        placement(&module.file, &mut errors);
    }
    if errors.is_empty() {
        Ok(localize(warnings, &labels))
    } else {
        Err(localize(errors, &labels))
    }
}

/// Checks the file, with the items of the modules it imports among its own.
fn check_file(
    file: File,
    items: Items,
    mut lazy: Lazy,
    mut errors: Vec<Error>,
    import_warnings: Vec<Error>,
    prelude: &Prelude,
    whole: bool,
) -> Result<Checked, Vec<Error>> {
    let mut loaded: Vec<usize> = Vec::new();
    let mut file = split_needs(with_prelude(file, prelude, &loaded));
    let trait_names: Vec<String> = file.traits.iter().map(|t| t.name.clone()).collect();
    let base_impls: Vec<Arc<Impl>> = std::mem::take(&mut file.impls);
    let (base_functions, base_structs, base_enums) =
        (file.functions.len(), file.structs.len(), file.enums.len());
    // What the file itself names of the modules is read first, and then what
    // that names.
    if !lazy.modules.is_empty() {
        let impls = base_impls.clone();
        let written = std::mem::take(&mut lazy.written);
        lazy.load(
            &[],
            &written,
            |out| names_in_items(&file, &impls, out),
            &trait_names,
            &mut errors,
        );
    }
    // The `impl`s of the prelude and the items of the modules that what the
    // program reaches calls for, which reach more, until no more are called
    // for.
    loop {
        file.functions.truncate(base_functions);
        file.structs.truncate(base_structs);
        file.enums.truncate(base_enums);
        file.impls = (loaded.iter().map(|&i| prelude.impl_at(i)))
            .chain(base_impls.iter().cloned())
            .collect();
        lazy.append(&mut file);
        let names = Names::of(&file);
        let Some(&start_at) = names
            .functions
            .get(START)
            .or_else(|| names.functions.get("main"))
        else {
            break;
        };
        let found = reach(&file, &names, &roots_of(&file, start_at, whole), true);
        let wanted = prelude.wanted(&file, &found.items, &loaded);
        let from_modules = lazy.wanted(&file, &names, &found);
        if wanted.is_empty() && from_modules.is_empty() {
            break;
        }
        loaded.extend(wanted);
        loaded.sort_unstable();
        lazy.load(&from_modules, &[], |_| {}, &trait_names, &mut errors);
    }

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
    let mut function_names: Map<&str, usize> = Map::default();
    let mut struct_names: Map<&str, usize> = Map::default();
    let mut enum_names: Map<&str, usize> = Map::default();
    let mut trait_names: Map<&str, usize> = Map::default();
    let mut trait_fns: Map<&str, Vec<(usize, usize)>> = Map::default();
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
            Claim::Struct(_) | Claim::Enum(_) => PRELUDE_TYPES.contains(&name),
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
                    // A function of the prelude's trait that no call needs a
                    // trait for is not a name the program may not use.
                    let taken = (trait_fns.get(name).and_then(|f| f.first()))
                        .filter(|(k, _)| !of_prelude(file.traits[*k].pos));
                    if let Some(&(k, _)) = taken {
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

    // Where the program starts: at `main`, or at the function that ends the
    // program with the status `main`'s result gives.
    let start_at = function_names.get(START).copied().unwrap_or(main);
    let names = Names {
        functions: function_names,
        structs: struct_names,
        enums: enum_names,
        traits: trait_names,
        trait_fns,
    };
    let roots = roots_of(&file, start_at, whole);
    let Reached {
        items: reached,
        unbuilt,
        ..
    } = reach(&file, &names, &roots, false);
    let reached_types: Vec<Def> = (reached.iter())
        .filter_map(|r| match r {
            Item::Struct(s) => Some(Def::Struct(*s)),
            Item::Enum(e) => Some(Def::Enum(*e)),
            _ => None,
        })
        .collect();

    let mut defs = Defs {
        file: &file,
        function_names: names.functions,
        struct_names: names.structs,
        enum_names: names.enums,
        trait_names: names.traits,
        trait_fns: names.trait_fns,
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
        fmt: prelude_item(&file, Kind::Trait, "Fmt"),
        repr: prelude_item(&file, Kind::Trait, "Repr"),
        eq: prelude_item(&file, Kind::Trait, "Eq"),
        exit: prelude_item(&file, Kind::Trait, "Exit"),
        iterate: prelude_item(&file, Kind::Trait, "Iterate"),
        ord: prelude_item(&file, Kind::Trait, "Ord"),
        hash: prelude_item(&file, Kind::Trait, "Hash"),
        convert: prelude_item(&file, Kind::Trait, "Convert"),
        bits: prelude_item(&file, Kind::Trait, "Bits"),
        serialize: library_item(&file, &items, "std.serialize", Kind::Trait, "Serialize"),
        writer: library_item(&file, &items, "std.serialize", Kind::Trait, "Writer"),
        deserialize: library_item(&file, &items, "std.deserialize", Kind::Trait, "Deserialize"),
        reader: library_item(&file, &items, "std.deserialize", Kind::Trait, "Reader"),
        read_error: library_item(&file, &items, "std.deserialize", Kind::Struct, "ReadError"),
        problem: library_item(&file, &items, "std.deserialize", Kind::Enum, "Problem"),
        boolean: prelude_item(&file, Kind::Enum, "bool"),
        order: prelude_item(&file, Kind::Enum, "Order"),
        option: prelude_item(&file, Kind::Enum, "Option"),
        flow: prelude_item(&file, Kind::Enum, "Flow"),
        result: prelude_item(&file, Kind::Enum, "Result"),
        withouts: Vec::new(),
        default_depth: Cell::new(0),
        spelled: Cell::new(Spelled::default()),
        bound_params: RefCell::new(Vec::new()),
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
        .map(|s| defs.scope(&s.generics, &[]))
        .collect();
    defs.fields = (file.structs.iter().enumerate())
        .map(|(i, s)| {
            (s.fields.iter())
                .map(|f| defs.quietly(&f.ty, &defs.struct_scopes[i]))
                .collect()
        })
        .collect();
    defs.enum_scopes = file
        .enums
        .iter()
        .map(|e| defs.scope(&e.generics, &[]))
        .collect();
    defs.cases = (file.enums.iter().enumerate())
        .map(|(i, e)| {
            (e.cases.iter())
                .map(|c| c.ty.as_ref().map(|t| defs.quietly(t, &defs.enum_scopes[i])))
                .collect()
        })
        .collect();
    defs.signatures = (file.functions.iter())
        .map(|f| defs.signature(f, defs.scope(&f.generics, &f.needs)))
        .collect();
    defs.trait_sigs = (file.traits.iter())
        .map(|t| {
            (t.functions.iter())
                .map(|f| {
                    // A function's own type parameters come after the trait's.
                    let generics: Vec<Generic> =
                        t.generics.iter().chain(&f.generics).cloned().collect();
                    let mut sig = defs.signature(f, defs.scope(&generics, &[]));
                    sig.param_defaults = Vec::new();
                    sig
                })
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
                // An implementation that requires something stands for fewer
                // types than it is written for, so a use decides.
                a.chosen
                    && a.scope.requires.is_empty()
                    && b.scope.requires.is_empty()
                    && overlaps(&defs.outs[k], a, b)
            });
            if covered && let Some(info) = &mut defs.impls[j] {
                info.chosen = false;
            }
        }
    }

    defs.withouts = withouts(&defs, &mut errors);

    let mut checker = Checker {
        defs: &defs,
        errors,
        types: HashMap::new(),
        widened: HashMap::new(),
        comparing: false,
        settled: false,
        value_calls: Set::default(),
        narrowed: HashSet::new(),
        fn_values: Map::default(),
        no_impl: false,
        refusals: 0,
        deciding: None,
    };
    checker.main(main);
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
            Item::Impl(i) if !unbuilt.contains(&i) => {
                if let Some(info) = &defs.impls[i] {
                    for (fi, at) in info.fns.iter().enumerate() {
                        if at.is_some() {
                            checker.body(Template::Impl(i, fi));
                        }
                    }
                }
            }
            Item::Impl(_) | Item::Struct(_) | Item::Enum(_) | Item::Trait(_) => {}
        }
    }
    checker.recursion(start_at, &mut Set::default(), &mut Set::default());
    if whole {
        return Err(checker.errors);
    }
    if !checker.errors.is_empty() {
        // What the function that starts the program says is wrong follows from
        // what `main` says, which is at a place the reader can see.
        if checker.errors.iter().any(|e| !of_prelude(e.pos)) {
            checker.errors.retain(|e| !of_prelude(e.pos));
        }
        return Err(checker.errors);
    }
    let start = Instance {
        template: Template::Free(start_at),
        args: Vec::new(),
    };
    let typed = Typed {
        defs: &defs,
        types: checker.types,
        widened: checker.widened,
        value_calls: checker.value_calls,
        fn_values: checker.fn_values,
    };
    let mut errors = Vec::new();
    typed.copies(
        &start,
        Pos::START,
        &mut Map::default(),
        &mut Set::default(),
        &mut errors,
    );
    if !errors.is_empty() {
        return Err(errors);
    }

    let program = to_scalar::lower(&typed, start);
    let prelude_reached = reached.iter().fold((0, 0), |(f, i), item| match *item {
        Item::Fn(k) if of_prelude(file.functions[k].pos) => (f + 1, i),
        Item::Impl(k) if of_prelude(file.impls[k].pos) => (f, i + 1),
        _ => (f, i),
    });
    Ok(Checked {
        program,
        prelude_reached,
        prelude_loaded: loaded.len(),
        warnings: {
            // An `impl` that is not built is dead code, as a function nothing calls is.
            let built: Vec<Item> = (reached.iter().copied())
                .filter(|item| !matches!(item, Item::Impl(i) if unbuilt.contains(i)))
                .collect();
            let mut all = warnings(&file, &built);
            all.extend(import_warnings);
            all
        },
    })
}

/// Where reaching starts: at the function the program starts at, and, in a
/// check of the library, at everything in it.
fn roots_of(file: &File, start_at: usize, whole: bool) -> Vec<Item> {
    let mut roots = vec![Item::Fn(start_at)];
    if whole {
        let in_std = |pos: Pos| of_std(pos);
        roots.extend(
            (file.functions.iter().enumerate())
                .filter(|(_, f)| in_std(f.pos))
                .map(|(i, _)| Item::Fn(i)),
        );
        roots.extend(
            (file.structs.iter().enumerate())
                .filter(|(_, s)| in_std(s.pos))
                .map(|(i, _)| Item::Struct(i)),
        );
        roots.extend(
            (file.enums.iter().enumerate())
                .filter(|(_, e)| in_std(e.pos))
                .map(|(i, _)| Item::Enum(i)),
        );
        roots.extend(
            (file.traits.iter().enumerate())
                .filter(|(_, t)| in_std(t.pos))
                .map(|(i, _)| Item::Trait(i)),
        );
        roots.extend(
            (file.impls.iter().enumerate())
                .filter(|(_, im)| in_std(im.pos))
                .map(|(i, _)| Item::Impl(i)),
        );
    }
    let mut seen: Set<Item> = Set::default();
    roots.retain(|r| seen.insert(*r));
    roots
}

/// What a name stands for in a file: the item that first defines it.
struct Names<'a> {
    functions: Map<&'a str, usize>,
    structs: Map<&'a str, usize>,
    enums: Map<&'a str, usize>,
    traits: Map<&'a str, usize>,
    trait_fns: Map<&'a str, Vec<(usize, usize)>>,
}

impl<'a> Names<'a> {
    /// The names of the items of the file, with no mistake reported.
    fn of(file: &'a File) -> Names<'a> {
        let mut names = Names {
            functions: Map::default(),
            structs: Map::default(),
            enums: Map::default(),
            traits: Map::default(),
            trait_fns: Map::default(),
        };
        for (i, f) in file.functions.iter().enumerate() {
            names.functions.entry(f.name.as_str()).or_insert(i);
        }
        for (i, s) in file.structs.iter().enumerate() {
            names.structs.entry(s.name.as_str()).or_insert(i);
        }
        for (i, e) in file.enums.iter().enumerate() {
            names.enums.entry(e.name.as_str()).or_insert(i);
        }
        for (k, t) in file.traits.iter().enumerate() {
            names.traits.entry(t.name.as_str()).or_insert(k);
            for (i, f) in t.functions.iter().enumerate() {
                let list = names.trait_fns.entry(f.name.as_str()).or_default();
                if !list.iter().any(|(t, _)| *t == k) {
                    list.push((k, i));
                }
            }
        }
        names
    }
}

/// What the function the program starts at reaches, each item in the order
/// it is first reached. A contract is not run, so what only a `requires` or an
/// `ensures` names is reached only when `contracts` says to look in them, as
/// choosing the prelude's `impl`s does: `by <= 30` there needs `Ord`.
///
/// An `impl` of a trait that is reached, for a type that is not, is in the
/// items but is not walked, and is in the set the call gives beside them: its
/// head is checked, and nothing it holds is, until its type is reached.
fn reach(file: &File, names: &Names, roots: &[Item], contracts: bool) -> Reached {
    let fmt = prelude_item(file, Kind::Trait, "Fmt");
    let mut reached: Vec<Item> = roots.to_vec();
    let mut queue: Vec<Item> = roots.to_vec();
    let mut unbuilt: Set<usize> = Set::default();
    // The structs and enums each `impl` is for.
    let mut for_types: Vec<Vec<Item>> = Vec::with_capacity(file.impls.len());
    let mut seen: Set<Item> = roots.iter().copied().collect();
    // The `impl`s that name a type in their arguments, and those of a trait,
    // each in order, so that an item finds its own without a walk of all.
    let mut for_type: Map<&str, Vec<usize>> = Map::default();
    let mut of_trait: Map<&str, Vec<usize>> = Map::default();
    for (i, im) in file.impls.iter().enumerate() {
        of_trait.entry(im.name.as_str()).or_default().push(i);
        let mut named: Vec<&str> = im
            .args
            .iter()
            .flat_map(type_names)
            .map(|(n, _)| n)
            .collect();
        named.sort_unstable();
        named.dedup();
        for name in named {
            for_type.entry(name).or_default().push(i);
        }
        // The types the `impl` is chosen by, which are those of the arguments
        // the trait does not write `out` for: an `out` type is the answer of
        // a call that chooses the `impl`, so it is reached with it.
        let outs = names
            .traits
            .get(im.name.as_str())
            .map(|&k| &file.traits[k].generics);
        let by = im
            .args
            .iter()
            .enumerate()
            .filter(|(j, _)| !outs.is_some_and(|g| g.get(*j).is_some_and(|g| g.out.is_some())));
        let mut types = Vec::new();
        for (name, _) in by.flat_map(|(_, t)| type_names(t)) {
            if let Some(&s) = names.structs.get(name) {
                types.push(Item::Struct(s));
            } else if let Some(&e) = names.enums.get(name) {
                types.push(Item::Enum(e));
            }
        }
        for_types.push(types);
    }
    let mut i = 0;
    while i < queue.len() {
        let mut found: Vec<Item> = Vec::new();
        let type_name = |name: &str, found: &mut Vec<Item>| {
            if let Some(&s) = names.structs.get(name) {
                found.push(Item::Struct(s));
            } else if let Some(&e) = names.enums.get(name) {
                found.push(Item::Enum(e));
            } else if let Some(&k) = names.traits.get(name) {
                found.push(Item::Trait(k));
            }
        };
        // A case written in full names its enum, and a function may be
        // passed as a value by its name.
        let called = |values: Vec<&Expr>, found: &mut Vec<Item>| {
            let all = exprs(values.iter().copied());
            for &e in &all {
                if let Expr::Field { of, .. } = e
                    && let Expr::Name { name, .. } = &**of
                {
                    type_name(name, found);
                }
                if let Expr::Name { name, .. } = e
                    && let Some(&f) = names.functions.get(name.as_str())
                {
                    found.push(Item::Fn(f));
                }
                // A `loop` over a value runs the `each` of its `Iterate`.
                if matches!(e, Expr::Loop(l) if l.over.is_some()) {
                    type_name("Iterate", found);
                }
            }
            let calls = all.iter().filter_map(|e| match e {
                Expr::Call(call) => Some(call),
                _ => None,
            });
            for call in calls {
                if let Some((q, _)) = &call.qualifier {
                    type_name(q, found);
                    continue;
                }
                let name = call.callee.as_str();
                if let Some(&f) = names.functions.get(name) {
                    found.push(Item::Fn(f));
                    type_name(name, found);
                    continue;
                }
                type_name(name, found);
                for &(k, _) in names.trait_fns.get(name).into_iter().flatten() {
                    found.push(Item::Trait(k));
                }
            }
        };
        let function = |f: &Function, found: &mut Vec<Item>| {
            for name in types_named(f) {
                type_name(name, found);
            }
            let defaults = f.params.iter().filter_map(|p| p.default.as_ref());
            let contracts = f.requires.iter().chain(&f.ensures).filter(|_| contracts);
            called(
                values(&f.body).chain(defaults).chain(contracts).collect(),
                found,
            );
        };
        match queue[i] {
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
                found.extend(
                    for_type
                        .get(s.name.as_str())
                        .into_iter()
                        .flatten()
                        .map(|&i| Item::Impl(i)),
                );
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
                found.extend(
                    for_type
                        .get(e.name.as_str())
                        .into_iter()
                        .flatten()
                        .map(|&i| Item::Impl(i)),
                );
            }
            Item::Trait(k) => {
                let t = &file.traits[k];
                // The `Fmt` a type is given runs its `Repr`.
                if k == fmt {
                    type_name("Repr", &mut found);
                }
                for f in &t.functions {
                    function(f, &mut found);
                }
                for name in bounds_named(&t.generics) {
                    type_name(name, &mut found);
                }
                for &i in of_trait.get(t.name.as_str()).into_iter().flatten() {
                    found.push(Item::Impl(i));
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
                for name in needs_named(&im.requires) {
                    type_name(name, &mut found);
                }
                for f in &im.functions {
                    function(f, &mut found);
                }
            }
        }
        for item in found {
            let held = match item {
                Item::Impl(k) => for_types[k].iter().all(|t| seen.contains(t)),
                _ => true,
            };
            if seen.insert(item) {
                reached.push(item);
                match item {
                    Item::Impl(k) if !held => {
                        unbuilt.insert(k);
                    }
                    _ => queue.push(item),
                }
            } else if let Item::Impl(k) = item
                && held
                && unbuilt.remove(&k)
            {
                queue.push(item);
            }
        }
        i += 1;
    }
    Reached {
        items: reached,
        unbuilt,
    }
}

/// What the program reaches.
struct Reached {
    /// The items, in the order each is first reached.
    items: Vec<Item>,
    /// The `impl`s whose heads are checked and nothing more.
    unbuilt: Set<usize>,
}

/// The modules of a library, each with its name and its source, and the files
/// of a project, which give the modules the standard library does not hold.
/// The standard library is the files of `std/`.
pub struct Library<'a> {
    modules: Vec<(&'a str, &'a str)>,
    project: Option<Project<'a>>,
}

/// The files a project's modules are in.
struct Project<'a> {
    /// The source of the module with a name written as in a `use`, as
    /// `db.transaction`, when a file holds it.
    files: &'a dyn crate::Source,
    /// The module the file that is built is, when its path gives it a name.
    /// Nothing imports from it.
    entry: Option<&'a str>,
}

impl<'a> Library<'a> {
    /// The library of the modules, given as a name and a source each.
    pub fn new(modules: &[(&'a str, &'a str)]) -> Library<'a> {
        Library {
            modules: modules.to_vec(),
            project: None,
        }
    }

    /// The standard library, which the executable holds.
    pub fn standard() -> Library<'static> {
        Library {
            modules: LIBRARY_MODULES
                .iter()
                .filter_map(|m| library_source(m).map(|s| (*m, s)))
                .collect(),
            project: None,
        }
    }

    /// The standard library, and the modules of a project, which `files` gives
    /// by their names. `entry` is the module the built file is.
    pub fn in_project(files: &'a dyn crate::Source, entry: Option<&'a str>) -> Library<'a> {
        Library {
            project: Some(Project { files, entry }),
            ..Library::standard()
        }
    }

    /// The source of the module with a name written as in a `use`.
    fn source(&self, module: &str) -> Option<std::borrow::Cow<'a, str>> {
        match module.split_once('.') {
            Some(("std", short)) => self
                .modules
                .iter()
                .find(|(m, _)| *m == short)
                .map(|(_, s)| (*s).into()),
            _ if module == "std" => None,
            _ => self.project.as_ref()?.files.source(module).map(Into::into),
        }
    }
}

/// The source of a module of the standard library, which errors in it show a
/// line of.
pub fn library_text(module: &str) -> &'static str {
    library_source(module).unwrap_or("")
}

/// How far apart the lines of two modules are, so that a place in a module is
/// told from one in the file or the prelude, and from one in another module,
/// by its line alone. The prelude is at line 0, the file below the first step,
/// and the module numbered `n` is at `n * STEP` and the lines after it.
const STEP: usize = 1 << 32;

/// The number of the first module of a project. Those of the standard library
/// are numbered below it, so that a place says which kind of module it is in.
const FIRST_PROJECT: usize = 1 << 16;

/// The module that holds the prelude, whose items are written as they are.
const PRELUDE_MODULE: &str = "std.prelude";

/// What kind of item of a module a lookup is for.
#[derive(Clone, Copy)]
enum Kind {
    Trait,
    Struct,
    Enum,
}

/// The name each item of each module of the standard library that the program
/// reads has in the file, by the module and the name the module gives it. The
/// file writes an item as the program lists it, as another name when the
/// program lists it with `as`, and with its module after it when the program
/// lists it not at all, so what the file calls an item says nothing of what
/// the item is.
#[derive(Clone, Default)]
struct Items(Vec<ItemNames>);

/// How a module of the standard library writes its names in the file: the
/// module, the names given with `as`, and the names written as they are.
type ItemNames = (String, Vec<(String, String)>, HashSet<String>);

/// The place, among the file's traits, structs, or enums, of the item that the
/// library defines with the name `name` in the module `module`, written as
/// `std.serialize`, or `None` when the program does not read the module.
/// Every lookup of an item of the library goes through this function, so that
/// what the compiler leans on is in the calls of it, and a name that `std/`
/// no longer defines stops the compiler at once, saying which.
fn library_item(file: &File, items: &Items, module: &str, kind: Kind, name: &str) -> Option<usize> {
    let (written, home): (String, fn(Pos) -> bool) = if module == PRELUDE_MODULE {
        (name.to_string(), of_prelude)
    } else {
        let (_, aliased, kept) = items.0.iter().find(|(m, ..)| m == module)?;
        if name.starts_with("__") {
            not_defined(module, kind, name)
        }
        (own_final(module, aliased, kept, name), of_std)
    };
    let at = match kind {
        Kind::Trait => (file.traits.iter()).position(|t| t.name == written.as_str() && home(t.pos)),
        Kind::Struct => {
            (file.structs.iter()).position(|s| s.name == written.as_str() && home(s.pos))
        }
        Kind::Enum => (file.enums.iter()).position(|e| e.name == written.as_str() && home(e.pos)),
    };
    Some(at.unwrap_or_else(|| not_defined(module, kind, name)))
}

/// An item of the prelude, which every program has.
fn prelude_item(file: &File, kind: Kind, name: &str) -> usize {
    library_item(file, &Items::default(), PRELUDE_MODULE, kind, name).expect("the prelude is read")
}

fn not_defined(module: &str, kind: Kind, name: &str) -> ! {
    let kind = match kind {
        Kind::Trait => "trait",
        Kind::Struct => "struct",
        Kind::Enum => "enum",
    };
    panic!(
        "internal error: the compiler needs the {kind} `{name}` of the module `{module}`, and the module does not define it"
    )
}

/// The module of the standard library, written as after `std.`, that a name
/// the prelude does not hold is imported from: the module that exports it,
/// the one that holds an effect the compiler builds, or `bits` for a size.
fn std_home(name: &str) -> Option<&'static str> {
    if PRELUDE_LISTED.contains(&name) {
        return None;
    }
    if SIZES.contains(&name) {
        return Some("bits");
    }
    if let Some((module, _)) = LIBRARY_EFFECTS_EXPORTED.iter().find(|(_, e)| *e == name) {
        return Some(module);
    }
    STD_EXPORTS
        .iter()
        .find(|(_, names)| names.contains(&name))
        .map(|(module, _)| *module)
}

/// Whether a name is one of the sizes, and the module is `std.bits`.
fn of_bits(module: &str, name: &str) -> bool {
    module == "std.bits" && SIZES.contains(&name)
}

/// Whether a definition is in a module of the library or of the project.
fn of_library(pos: Pos) -> bool {
    pos.line >= STEP
}

/// Whether a definition is in a module of the standard library, which the
/// compiler builds functions for.
fn of_std(pos: Pos) -> bool {
    (STEP..FIRST_PROJECT * STEP).contains(&pos.line)
}

/// The number of the module a place is in: 0 for the file and the prelude.
fn module_of(pos: Pos) -> usize {
    pos.line / STEP
}

/// Whether the field of a struct is private to the module that defines the
/// struct, and `at` is in another module. The prelude and the file are one
/// module, so a field of a struct of the prelude is read by any file.
fn hidden(def: &crate::parser::Struct, field: &crate::parser::Field, at: Pos) -> bool {
    field.public.is_none() && of_library(def.pos) && module_of(def.pos) != module_of(at)
}

/// A name as its module wrote it, without the module that renaming adds.
fn written_name(name: &str) -> &str {
    name.split('@').next().unwrap_or(name)
}

/// The error for a field that code outside its module reads or assigns. A
/// program cannot change a module of the library, so the fix there names the
/// function of that module that gives the field, as `raw_text` gives `text`,
/// when it has one.
fn private_field(def: &crate::parser::Struct, field: &str, at: Pos) -> Error {
    let name = written_name(&def.name);
    let error = Error::new(
        at,
        format!("the field `{field}` of `{name}` is private to the module that defines it"),
    );
    if !of_library(def.pos) {
        return error.with_fix(format!(
            "write `pub` before `{field}` where `{name}` is defined"
        ));
    }
    // The module that defines the struct writes the function in its source.
    let gives = LIBRARY_MODULES.iter().find_map(|m| {
        let source = library_text(m);
        if !source.contains(&format!("pub struct {name} {{")) {
            return None;
        }
        source.lines().find_map(|line| {
            let head = line.strip_prefix("pub fn ")?;
            let (called, rest) = head.split_once('(')?;
            let own = rest.split_once(')')?.0;
            let takes = own.split_once(": ").is_some_and(|(_, ty)| ty == name);
            (called.ends_with(&format!("_{field}")) && takes).then(|| called.to_string())
        })
    });
    match gives {
        Some(f) => error.with_fix(format!(
            "read it with `{f}`, which the module that defines `{name}` gives"
        )),
        None => error.with_fix(format!(
            "`{field}` is the module's own, and the module gives no function for it"
        )),
    }
}

/// The name of each module the places of a file say, by its number, as the map
/// of an executable writes it: `std.console`, `db.transaction`, and for the
/// file that is built, which is number 0, the module its path gives it, or
/// `main` when it has none.
fn module_names(labels: &[(usize, String)], library: &Library) -> Vec<(usize, String)> {
    let entry = library.project.as_ref().and_then(|p| p.entry);
    let mut names = vec![(0, entry.unwrap_or("main").to_string())];
    for (number, label) in labels {
        let name = if *number < FIRST_PROJECT {
            format!("std.{label}")
        } else {
            label.trim_end_matches(".smith").replace('/', ".")
        };
        names.push((*number, name));
    }
    names
}

/// The errors with a place in a module, given as that module and the place
/// in its file. `labels` are the modules read, each by its number.
fn localize(errors: Vec<Error>, labels: &[(usize, String)]) -> Vec<Error> {
    errors
        .into_iter()
        .map(|mut e| {
            if of_library(e.pos)
                && let Some((_, label)) = labels.iter().find(|(n, _)| *n == module_of(e.pos))
            {
                e.module = Some(label.clone());
                e.pos.line %= STEP;
            }
            e
        })
        .collect()
}

/// The items of a module of the standard library, which are read when the
/// program reaches them: where each is in the source, and which are read.
struct Pieces {
    list: Vec<Piece>,
    /// The places in `list` of the items that are neither a `use` nor an
    /// `impl`, in the order of their names.
    named: Vec<u32>,
    /// The places of the `impl`s, in the order of their traits' names.
    impls: Vec<u32>,
    /// Which are read.
    loaded: Vec<bool>,
}

impl Pieces {
    fn new(source: &str, list: Vec<Piece>) -> Pieces {
        let name = |i: &u32| &source[list[*i as usize].name.0..list[*i as usize].name.1];
        let mut named: Vec<u32> = Vec::with_capacity(list.len());
        let mut impls: Vec<u32> = Vec::new();
        for (i, p) in list.iter().enumerate() {
            match p.kind {
                PieceKind::Use => {}
                PieceKind::Impl => impls.push(i as u32),
                _ => named.push(i as u32),
            }
        }
        named.sort_by(|a, b| name(a).cmp(name(b)));
        impls.sort_by(|a, b| name(a).cmp(name(b)));
        let loaded = vec![false; list.len()];
        Pieces {
            list,
            named,
            impls,
            loaded,
        }
    }

    /// The items with the name, among `of`, which is `named` or `impls`.
    fn find<'p>(&'p self, source: &str, of: &'p [u32], name: &str) -> &'p [u32] {
        let at = |i: &u32| {
            let p = &self.list[*i as usize];
            &source[p.name.0..p.name.1]
        };
        let from = of.partition_point(|i| at(i) < name);
        let to = from + of[from..].partition_point(|i| at(i) == name);
        &of[from..to]
    }
}

/// A module that a `use` names, read.
struct Module<'a> {
    /// The source, which the places of `pieces` are in.
    source: std::borrow::Cow<'a, str>,
    /// The items that are read when they are reached, when the module is
    /// divided into them.
    pieces: Option<Pieces>,
    /// The names the module defines that are read with it, which are all of
    /// them unless it is divided.
    defined: HashSet<String>,
    /// The names the compiler builds, which the module's source only wraps.
    held: HashSet<String>,
    /// The name as a `use` writes it, such as `std.list` or `db.transaction`.
    name: String,
    /// What an error in it says the module is: the name of a module of the
    /// standard library, and the path of the file of a module of a project.
    label: String,
    /// The tokens of the file, at the lines of the module's number.
    tokens: Vec<crate::lexer::Token>,
    file: File,
    /// The names a `use` may list: those its source writes with `pub`, the
    /// functions of the traits it writes with `pub`, and the effects the
    /// compiler holds for it.
    exports: Vec<String>,
    /// The effects among the exports, which the module does not define.
    effects: Vec<String>,
    /// The names the module lists from others, each with the module it takes
    /// it from, by its place among those read.
    imports: Vec<Import>,
    /// The modules the module imports itself, each by the name it is written
    /// as, with the module by its place among those read.
    aliases: Vec<(String, usize)>,
    /// The names the file that is built lists from it, as they are.
    entry_imported: Vec<String>,
    /// The names the file that is built lists from it with `as`, each with
    /// the name it is given.
    entry_aliased: Vec<(String, String)>,
    /// The number the module's places are at, as `STEP` says.
    number: usize,
}

impl Module<'_> {
    /// Whether the module defines the name.
    fn defines(&self, name: &str) -> bool {
        self.defined.contains(name)
            || self
                .pieces
                .as_ref()
                .is_some_and(|p| !p.find(&self.source, &p.named, name).is_empty())
    }

    /// Whether the module exports the name.
    fn exports(&self, name: &str) -> bool {
        self.exports.iter().any(|e| e == name)
            || self.pieces.as_ref().is_some_and(|p| {
                p.find(&self.source, &p.named, name)
                    .iter()
                    .any(|&i| p.list[i as usize].public)
            })
    }
}

/// A name a module lists from another.
struct Import {
    /// The name in the module that lists it, which `as` may have given.
    written: String,
    /// The name in the module it is from.
    name: String,
    /// The module it is from, by its place among those read.
    from: usize,
}

/// The tokens with each place moved to the module's lines.
fn shifted(tokens: &[crate::lexer::Token], base: usize) -> Vec<crate::lexer::Token> {
    use crate::lexer::{Piece, Tok};
    tokens
        .iter()
        .map(|t| {
            let tok = match &t.tok {
                Tok::Interp(pieces) => Tok::Interp(
                    pieces
                        .iter()
                        .map(|p| match p {
                            Piece::Expr(inner) => Piece::Expr(shifted(inner, base)),
                            text => text.clone(),
                        })
                        .collect(),
                ),
                other => other.clone(),
            };
            let mut pos = t.pos;
            pos.line += base;
            crate::lexer::Token { tok, pos }
        })
        .collect()
}

/// The names a file defines: its functions, types, and traits, and the
/// functions of its traits.
fn defined_names(file: &File) -> HashSet<&str> {
    (file.functions.iter().map(|f| f.name.as_str()))
        .chain(file.structs.iter().map(|s| s.name.as_str()))
        .chain(file.enums.iter().map(|e| e.name.as_str()))
        .chain(file.traits.iter().flat_map(|t| {
            std::iter::once(t.name.as_str()).chain(t.functions.iter().map(|f| f.name.as_str()))
        }))
        .collect()
}

/// The modules a file imports, read once each, and what each file lists from
/// them.
struct Resolver<'l, 'a> {
    library: &'l Library<'a>,
    /// Whether every module is read whole, as a check of the library does.
    whole: bool,
    read: Vec<Module<'a>>,
    /// Every module a read was tried for, by number, with its label.
    labels: Vec<(usize, String)>,
    /// The place among `read` of each module read, by name. `None` is one
    /// whose file is wrong, which is said once.
    by_name: HashMap<String, Option<usize>>,
    /// The modules whose own `use` lines are being read, the last the newest.
    open: Vec<String>,
    errors: Vec<Error>,
    warnings: Vec<Error>,
    /// Where a name written with a module is refused, which the checker
    /// would refuse again as a call of a name that is not there.
    refused: Vec<Pos>,
    /// The errors for names of the standard library that a file uses and does
    /// not import, which `refused` must not remove.
    unimported: Vec<Error>,
    next_std: usize,
    next_project: usize,
    /// The module of the standard library that exports each name, the first of
    /// them where two do, which is worked out when an error first needs it.
    homes: std::cell::OnceCell<HashMap<String, &'a str>>,
}

/// Whose `use` lines are being read.
#[derive(Clone, Copy)]
enum Reader {
    /// The file that is built.
    Entry,
    /// A module, by its place among those read.
    Module(usize),
}

impl<'l, 'a> Resolver<'l, 'a> {
    fn new(library: &'l Library<'a>) -> Self {
        Resolver {
            library,
            whole: false,
            read: Vec::new(),
            labels: Vec::new(),
            by_name: HashMap::new(),
            open: Vec::new(),
            errors: Vec::new(),
            warnings: Vec::new(),
            refused: Vec::new(),
            unimported: Vec::new(),
            next_std: 1,
            next_project: FIRST_PROJECT,
            homes: std::cell::OnceCell::new(),
        }
    }

    /// The module of the standard library that exports each name. A module's
    /// source is not lexed or parsed for it: an item that a module exports is
    /// written with `pub` at the start of a line, as `fn`, `struct`, `enum`, or
    /// `trait`, so the names are read off those lines.
    fn homes(&self) -> &HashMap<String, &'a str> {
        self.homes.get_or_init(|| {
            let mut homes = HashMap::new();
            for (module, _) in &self.library.modules {
                let Some(source) = self.library.source(&format!("std.{module}")) else {
                    continue;
                };
                for line in source.lines() {
                    let Some(rest) = ["pub fn ", "pub struct ", "pub enum ", "pub trait "]
                        .into_iter()
                        .find_map(|start| line.strip_prefix(start))
                    else {
                        continue;
                    };
                    let end = rest
                        .find(|c: char| !(c.is_alphanumeric() || c == '_'))
                        .unwrap_or(rest.len());
                    homes.entry(rest[..end].to_string()).or_insert(*module);
                }
            }
            homes
        })
    }

    /// Reads the module with the name, or says what is wrong in it. A module
    /// of the standard library is divided into its items, and an item is read
    /// when the program reaches it.
    fn read(&mut self, name: &str) -> Option<usize> {
        let source = self.library.source(name)?;
        let standard = name.starts_with("std.");
        let (label, number) = if standard {
            self.next_std += 1;
            (name[4..].to_string(), self.next_std - 1)
        } else {
            self.next_project += 1;
            (
                format!("{}.smith", name.replace('.', "/")),
                self.next_project - 1,
            )
        };
        self.labels.push((number, label.clone()));
        let divided = if standard && !self.whole {
            outline(&source)
        } else {
            None
        };
        if let Some(list) = divided {
            return self.read_divided(name, source, list, label, number);
        }
        let tokens = match crate::lexer::lex(source.as_bytes()) {
            Ok(tokens) => shifted(&tokens, number * STEP),
            Err(errors) => {
                self.errors
                    .extend(errors.into_iter().map(|e| e.in_module(&label)));
                self.by_name.insert(name.to_string(), None);
                return None;
            }
        };
        let mut file = match crate::parser::parse_library(&tokens) {
            Ok(file) => file,
            Err(errors) => {
                self.errors.extend(errors);
                self.by_name.insert(name.to_string(), None);
                return None;
            }
        };
        self.errors.append(&mut file.unbuilt);
        let defined: HashSet<String> = defined_names(&file).into_iter().map(String::from).collect();
        let held = held_by_the_compiler(&file)
            .into_iter()
            .map(String::from)
            .collect();
        let effects: Vec<String> = LIBRARY_EFFECTS_EXPORTED
            .iter()
            .filter(|(m, _)| format!("std.{m}") == name)
            .map(|(_, effect)| effect.to_string())
            .collect();
        let mut exports = file.exports.clone();
        exports.extend(
            (file.traits.iter())
                .filter(|t| file.exports.contains(&t.name))
                .flat_map(|t| t.functions.iter().map(|f| f.name.clone())),
        );
        exports.extend(effects.iter().cloned());
        let module = Module {
            source,
            pieces: None,
            defined,
            held,
            name: name.to_string(),
            label,
            tokens,
            file,
            exports,
            effects,
            imports: Vec::new(),
            aliases: Vec::new(),
            entry_imported: Vec::new(),
            entry_aliased: Vec::new(),
            number,
        };
        self.open_module(module)
    }

    /// Reads a module of the standard library that is divided into items: its
    /// `use` lines, its traits, and what the compiler leans on, and the places
    /// of the rest.
    fn read_divided(
        &mut self,
        name: &str,
        source: std::borrow::Cow<'a, str>,
        list: Vec<Piece>,
        label: String,
        number: usize,
    ) -> Option<usize> {
        let mut file = File::default();
        let mut held: HashSet<String> = HashSet::new();
        for p in &list {
            let text = &source[p.text.0..p.text.1];
            let word = &source[p.name.0..p.name.1];
            let read_now = match p.kind {
                PieceKind::Use | PieceKind::Trait => true,
                PieceKind::Struct | PieceKind::Enum => LEANED_ON.contains(&(name, word)),
                PieceKind::Fn | PieceKind::Impl => false,
            };
            // A function whose whole body calls what the compiler builds is
            // read to tell whether it is one.
            let wrapper = p.kind == PieceKind::Fn && text.contains("__");
            if p.kind != PieceKind::Use
                && p.kind != PieceKind::Impl
                && PRELUDE_TYPES.contains(&word)
                && matches!(p.kind, PieceKind::Struct | PieceKind::Enum)
            {
                held.insert(word.to_string());
            }
            if !read_now && !wrapper {
                continue;
            }
            let tokens = match crate::lexer::lex(text.as_bytes()) {
                Ok(tokens) => shifted(&tokens, number * STEP + p.line),
                Err(errors) => {
                    self.errors
                        .extend(errors.into_iter().map(|e| e.in_module(&label)));
                    self.by_name.insert(name.to_string(), None);
                    return None;
                }
            };
            let mut found = match crate::parser::parse_library(&tokens) {
                Ok(found) => found,
                Err(errors) => {
                    self.errors.extend(errors);
                    self.by_name.insert(name.to_string(), None);
                    return None;
                }
            };
            if wrapper {
                held.extend(
                    found
                        .functions
                        .iter()
                        .filter(|f| is_wrapper(f))
                        .map(|f| f.name.clone()),
                );
            }
            if read_now {
                file.uses.append(&mut found.uses);
                file.traits.append(&mut found.traits);
                file.structs.append(&mut found.structs);
                file.enums.append(&mut found.enums);
                file.exports.append(&mut found.exports);
                self.errors.append(&mut found.unbuilt);
            }
        }
        held.extend(held_by_the_compiler(&file).into_iter().map(String::from));
        let defined: HashSet<String> = defined_names(&file).into_iter().map(String::from).collect();
        let effects: Vec<String> = LIBRARY_EFFECTS_EXPORTED
            .iter()
            .filter(|(m, _)| format!("std.{m}") == name)
            .map(|(_, effect)| effect.to_string())
            .collect();
        let mut exports = file.exports.clone();
        exports.extend(
            (file.traits.iter())
                .filter(|t| file.exports.contains(&t.name))
                .flat_map(|t| t.functions.iter().map(|f| f.name.clone())),
        );
        exports.extend(effects.iter().cloned());
        let pieces = Pieces::new(&source, list);
        let module = Module {
            source,
            pieces: Some(pieces),
            defined,
            held,
            name: name.to_string(),
            label,
            tokens: Vec::new(),
            file,
            exports,
            effects,
            imports: Vec::new(),
            aliases: Vec::new(),
            entry_imported: Vec::new(),
            entry_aliased: Vec::new(),
            number,
        };
        self.open_module(module)
    }

    /// Adds the module read, and reads the modules its own `use` lines name,
    /// while it is open, so that a module that reaches itself again is told.
    fn open_module(&mut self, module: Module<'a>) -> Option<usize> {
        let name = module.name.clone();
        let divided = module.pieces.is_some();
        let slot = self.read.len();
        self.read.push(module);
        self.by_name.insert(name.clone(), Some(slot));
        self.open.push(name);
        let uses = self.read[slot].file.uses.clone();
        if divided {
            // Nothing in a module of the standard library is wrong, and
            // what it writes is not looked for.
            let written: HashSet<String> = uses
                .iter()
                .flat_map(|u| &u.names)
                .map(|n| n.alias.as_ref().map_or(n.name.clone(), |(a, _)| a.clone()))
                .collect();
            self.resolve(
                Reader::Module(slot),
                &uses,
                &HashSet::new(),
                &written,
                &[],
                &[],
            );
        } else {
            let defined = self.read[slot].defined.clone();
            let written = self.read[slot].file.written.clone();
            let qualified = self.read[slot].file.qualified.clone();
            let used = scope::used_names(&self.read[slot].file);
            self.resolve(
                Reader::Module(slot),
                &uses,
                &defined,
                &written,
                &qualified,
                &used,
            );
        }
        self.open.pop();
        Some(slot)
    }

    /// The place among `read` of the module a `use` names, or an error at the
    /// module's name.
    fn module(&mut self, module: &str, at: Pos) -> Option<usize> {
        if let Some(project) = &self.library.project
            && project.entry == Some(module)
        {
            self.errors.push(
                Error::new(
                    at,
                    format!("`{module}` is the file being built, and nothing imports from it"),
                )
                .with_fix("move what the other module needs into a module of its own"),
            );
            return None;
        }
        match self.by_name.get(module) {
            Some(Some(_)) if self.open.iter().any(|m| m == module) => {
                let from = self.open.iter().position(|m| m == module).unwrap_or(0);
                let ring: Vec<String> =
                    self.open[from..].iter().map(|m| format!("`{m}`")).collect();
                self.errors.push(
                    Error::new(
                        at,
                        format!(
                            "{} import each other in a ring, so no order builds them",
                            ring.join(", ")
                        ),
                    )
                    .with_fix("move what they all need into a module that imports none of them"),
                );
                None
            }
            Some(slot) => *slot,
            None => {
                let found = self.read(module);
                if found.is_none() && !self.by_name.contains_key(module) {
                    let standard = module.split('.').next() == Some("std");
                    let mut error = Error::new(at, format!("there is no module `{module}`"));
                    error = if standard {
                        let known: Vec<String> = self
                            .library
                            .modules
                            .iter()
                            .map(|(m, _)| format!("`std.{m}`"))
                            .collect();
                        error.with_fix(format!("import from one of {}", known.join(", ")))
                    } else {
                        error.with_fix(format!(
                            "write the module in the file `{}.smith`",
                            module.replace('.', "/")
                        ))
                    };
                    self.errors.push(error);
                }
                found
            }
        }
    }

    /// Checks the `use` lines of a file: each names a module and lists names it
    /// exports, once each, none of which the file defines or the prelude holds,
    /// and no name comes from two modules. A name is the one `as` gives it
    /// when it has one, and a module imported itself is a name too, which
    /// is its last segment unless `as` gives another. Each module named is
    /// read, once, and its own `use` lines are checked in turn. A name that is
    /// listed and never written in the file is a warning, as is a module
    /// imported and never written before a name.
    fn resolve(
        &mut self,
        reader: Reader,
        uses: &[Use],
        defined: &HashSet<String>,
        written: &HashSet<String>,
        qualified: &[Qualified],
        used: &[(String, Pos)],
    ) {
        let mut modules: Vec<String> = Vec::new();
        let mut taken: Vec<(String, String)> = Vec::new();
        // The modules imported themselves: the name each is written as, the
        // module, and its place among those read.
        let mut named: Vec<(String, String, usize)> = Vec::new();
        for u in uses {
            let parts: Vec<&str> = u.module.iter().map(|(n, _)| n.as_str()).collect();
            let module = parts.join(".");
            let at = u.module[0].1;
            let Some(slot) = self.module(&module, at) else {
                continue;
            };
            if modules.contains(&module) {
                self.errors.push(
                    Error::new(
                        at,
                        format!("`{module}` is already listed by an earlier `use`"),
                    )
                    .with_fix(format!(
                        "list every name from `{module}` in its first `use`"
                    )),
                );
                continue;
            }
            modules.push(module.clone());
            if !u.listed {
                let (called, pos) = u.called();
                let error = if let Some((_, other, _)) = named.iter().find(|(n, ..)| n == called) {
                    Some(
                        Error::new(
                            pos,
                            format!("`{module}` and `{other}` are both `{called}`: a name means one thing"),
                        )
                        .with_fix(format!("write `as` after `{module}` to give it another name")),
                    )
                } else {
                    self.clash(called, pos, defined, &taken, "a module")
                };
                match error {
                    Some(error) => self.errors.push(error),
                    None => {
                        named.push((called.to_string(), module.clone(), slot));
                        if let Reader::Module(of) = reader {
                            self.read[of].aliases.push((called.to_string(), slot));
                        }
                        if !qualified.iter().any(|q| q.written.0 == called) {
                            self.warnings.push(
                                Error::new(pos, format!("`{module}` is imported and never used"))
                                    .with_fix("delete the `use`"),
                            );
                        }
                    }
                }
                continue;
            }
            let mut listed: Vec<&str> = Vec::new();
            for item in &u.names {
                let (name, pos) = (item.name.as_str(), item.pos);
                let written_as = item.alias.as_ref().map_or(name, |(a, _)| a.as_str());
                let at_name = item.alias.as_ref().map_or(pos, |(_, at)| *at);
                let error = if listed.contains(&name) {
                    Some(
                        Error::new(pos, format!("`{name}` is listed twice from `{module}`"))
                            .with_fix(format!("list `{name}` once")),
                    )
                } else if item.alias.is_none() && defined.contains(name) {
                    Some(
                        Error::new(
                            pos,
                            format!(
                                "`{name}` is imported, and this file defines it: a name means one thing"
                            ),
                        )
                        .with_fix(format!(
                            "rename the definition, or delete `{name}` from the `use`"
                        )),
                    )
                } else if item.alias.is_none() && PRELUDE_LISTED.contains(&name) {
                    Some(
                        Error::new(
                            pos,
                            format!("`{name}` is in the prelude, so it needs no `use`"),
                        )
                        .with_fix(format!("delete `{name}` from the `use`")),
                    )
                } else if !self.read[slot].exports(name) && !of_bits(&module, name) {
                    Some(self.missing(&module, slot, name, pos))
                } else if item.alias.is_some() && (self.fixed(slot, name) || of_bits(&module, name))
                {
                    Some(Error::unsupported(
                        at_name,
                        "`as` on a name the compiler builds",
                    ))
                } else if item.alias.is_some() && self.of_trait(slot, name) {
                    Some(Error::unsupported(at_name, "`as` on a function of a trait"))
                } else if item.alias.is_some()
                    && let Some(error) = self.clash(written_as, at_name, defined, &taken, "a name")
                {
                    Some(error)
                } else if let Some((_, from)) = taken.iter().find(|(n, _)| n == written_as) {
                    Some(
                        Error::new(
                            at_name,
                            format!("`{written_as}` is listed from `{from}` and from `{module}`: a name means one thing"),
                        )
                        .with_fix(format!("delete `{name}` from one of them, or give it another name with `as`")),
                    )
                } else if let Some((_, other, _)) = named.iter().find(|(n, ..)| n == written_as) {
                    Some(Error::new(
                        at_name,
                        format!(
                            "`{written_as}` is the name of the module `{other}`: a name means one thing"
                        ),
                    ))
                } else {
                    None
                };
                listed.push(name);
                match error {
                    Some(error) => self.errors.push(error),
                    None => {
                        match reader {
                            Reader::Entry => match &item.alias {
                                Some((alias, _)) => self.read[slot]
                                    .entry_aliased
                                    .push((name.to_string(), alias.clone())),
                                None => self.read[slot].entry_imported.push(name.to_string()),
                            },
                            Reader::Module(of) => self.read[of].imports.push(Import {
                                written: written_as.to_string(),
                                name: name.to_string(),
                                from: slot,
                            }),
                        }
                        taken.push((written_as.to_string(), module.clone()));
                        if !written.contains(written_as) {
                            self.warnings.push(
                                Error::new(
                                    at_name,
                                    format!(
                                        "`{written_as}` is imported from `{module}` and never used"
                                    ),
                                )
                                .with_fix(format!("delete `{name}` from the `use`")),
                            );
                        }
                    }
                }
            }
        }
        self.unimported(reader, uses, defined, &named, used);
        for q in qualified {
            let Some((_, module, slot)) = named.iter().find(|(n, ..)| *n == q.written.0) else {
                continue;
            };
            let (name, pos) = (q.name.0.as_str(), q.name.1);
            if !self.read[*slot].exports(name) {
                let error = self.missing(module, *slot, name, pos);
                self.errors.push(error);
                self.refused.push(q.written.1);
            } else if self.fixed(*slot, name) {
                self.errors.push(Error::unsupported(
                    pos,
                    "calling a name the compiler builds through its module",
                ));
                self.refused.push(q.written.1);
            }
        }
    }

    /// An error at the first use of each name of the standard library that
    /// the file writes, and neither defines nor lists nor is a module's own
    /// export. The fix names the module to import it from. Every use of the
    /// name is refused, as the checker would say of the others that there is
    /// no such name.
    fn unimported(
        &mut self,
        reader: Reader,
        uses: &[Use],
        defined: &HashSet<String>,
        named: &[(String, String, usize)],
        used: &[(String, Pos)],
    ) {
        let own: Vec<&str> = match reader {
            Reader::Entry => Vec::new(),
            Reader::Module(of) => (self.read[of].exports.iter())
                .chain(&self.read[of].effects)
                .map(String::as_str)
                .chain(
                    if self.read[of].name == "std.bits" {
                        SIZES
                    } else {
                        &[]
                    }
                    .iter()
                    .copied(),
                )
                .collect(),
        };
        let listed: Vec<&str> = (uses.iter().flat_map(|u| &u.names))
            .map(|n| {
                n.alias
                    .as_ref()
                    .map_or(n.name.as_str(), |(a, _)| a.as_str())
            })
            .collect();
        let mut first: Vec<&str> = Vec::new();
        let mut errors = Vec::new();
        for (name, pos) in used {
            let Some(module) = std_home(name) else {
                continue;
            };
            if defined.contains(name.as_str())
                || own.contains(&name.as_str())
                || listed.contains(&name.as_str())
                || named.iter().any(|(n, ..)| n == name)
            {
                continue;
            }
            self.refused.push(*pos);
            if first.contains(&name.as_str()) {
                continue;
            }
            first.push(name);
            errors.push(
                Error::new(
                    *pos,
                    format!("`{name}` is in `std.{module}`, and this file does not import it"),
                )
                .with_fix(format!("add `use std.{module} ({name})`")),
            );
        }
        self.unimported.extend(errors);
    }

    /// The error for a name that a file gives to a module or to another
    /// name, when the file already has the name: it defines it, the prelude
    /// holds it, or an earlier `use` took it.
    fn clash(
        &self,
        name: &str,
        pos: Pos,
        defined: &HashSet<String>,
        taken: &[(String, String)],
        what: &str,
    ) -> Option<Error> {
        if defined.contains(name) {
            Some(
                Error::new(
                    pos,
                    format!("`{name}` is given to {what}, and this file defines it: a name means one thing"),
                )
                .with_fix("write `as` to give it another name"),
            )
        } else if PRELUDE_LISTED.contains(&name) {
            Some(
                Error::new(pos, format!("`{name}` is given to {what}, and the prelude holds it: a name means one thing"))
                    .with_fix("write `as` to give it another name"),
            )
        } else {
            taken.iter().find(|(n, _)| n == name).map(|(_, from)| {
                Error::new(
                    pos,
                    format!("`{name}` is given to {what}, and is already listed from `{from}`: a name means one thing"),
                )
                .with_fix("write `as` to give it another name")
            })
        }
    }

    /// Whether a name of a module is one the compiler builds, and writes as
    /// it is: an effect, a function of a trait of the library, and a name the
    /// module only wraps.
    fn fixed(&self, slot: usize, name: &str) -> bool {
        let module = &self.read[slot];
        module.name.starts_with("std.")
            && (module.effects.iter().any(|e| e == name)
                || (module.file.traits.iter()).any(|t| t.functions.iter().any(|f| f.name == name))
                || module.held.contains(name))
    }

    /// Whether a name of a module is a function of one of its traits.
    fn of_trait(&self, slot: usize, name: &str) -> bool {
        (self.read[slot].file.traits.iter()).any(|t| t.functions.iter().any(|f| f.name == name))
    }

    /// The error for a name that a module does not export: one it keeps
    /// private, or one it does not have.
    fn missing(&self, module: &str, slot: usize, name: &str, pos: Pos) -> Error {
        let read = &self.read[slot];
        if read.defines(name) {
            return Error::new(pos, format!("`{name}` is private to `{module}`"))
                .with_fix(format!("write `pub` before `{name}` in `{}`", read.label));
        }
        let mut error = Error::new(pos, format!("`{module}` has no name `{name}`"));
        let home = self.homes().get(name).copied();
        if let Some(home) = home {
            error = error.with_fix(format!("write `use std.{home} ({name})`"));
        } else if SIZES.contains(&name) {
            error = error.with_fix(format!("write `use std.bits ({name})`"));
        }
        error
    }
}

/// Whether a function's whole body is one call of a function the compiler
/// builds, with the function's own parameters in order.
fn is_wrapper(f: &Arc<Function>) -> bool {
    matches!(&f.body[..], [Stmt::Expr(Expr::Call(c))]
    if c.qualifier.is_none()
        && c.callee.starts_with("__")
        && c.args.len() == f.params.len()
        && c.args.iter().zip(&f.params).all(|(a, p)| {
            a.name.is_none()
                && !a.after
                && matches!(&a.value, Expr::Name { name, .. } if *name == p.name)
        }))
}

/// Whether the compiler holds the item of a module already, and builds it
/// itself: a function whose body is one call of a function the compiler
/// builds with its parameters, in order, and a type or a trait the prelude defines. The module's source
/// names these, and the compiler supplies them as it did before the bodies
/// of a module were compiled.
fn held_by_the_compiler(module: &File) -> HashSet<&str> {
    (module
        .functions
        .iter()
        .filter(|f| is_wrapper(f))
        .map(|f| f.name.as_str()))
    .chain(
        (module.structs.iter().map(|s| s.name.as_str()))
            .chain(module.enums.iter().map(|e| e.name.as_str()))
            .filter(|n| PRELUDE_TYPES.contains(n)),
    )
    .chain(
        (module.traits.iter().map(|t| t.name.as_str()))
            .filter(|n| PRELUDE_TRAITS.contains(n) || TRAITS_NOT_BUILT.contains(n)),
    )
    .collect()
}

/// The file with the items of the modules it imports among its own. A name
/// the file lists keeps its name. Every other name of a module is its own,
/// written with the module after it, so that it is not the name of anything in
/// the file or in another module, and the file cannot write it. A module that
/// lists names of another writes them as that module does. Items are checked
/// and compiled only when the program reaches them, as the file's are, and an
/// item of a module of the standard library is not read until then.
fn splice<'a>(
    mut file: File,
    mut modules: Vec<Module<'a>>,
    errors: &mut Vec<Error>,
) -> (File, Items, Lazy<'a>) {
    // The names each module writes as they are, since the file lists them, or
    // since the compiler builds them.
    let kept: Vec<HashSet<String>> = modules
        .iter()
        .map(|module| {
            let standard = module.name.starts_with("std.");
            let mut kept: HashSet<String> = module.entry_imported.iter().cloned().collect();
            kept.extend(module.effects.iter().cloned());
            if standard {
                kept.extend(
                    (module.file.traits.iter())
                        .filter(|t| module.exports.contains(&t.name))
                        .flat_map(|t| t.functions.iter().map(|f| f.name.clone())),
                );
                kept.extend(module.held.iter().cloned());
            }
            kept
        })
        .collect();
    // What a name a module defines is written as in the file: as it is, as
    // the file that is built names it with `as`, or with the module after it.
    let final_of = |from: usize, name: &str| -> Option<String> {
        let module = &modules[from];
        let standard = module.name.starts_with("std.");
        if !module.defines(name) || (standard && name.starts_with("__")) {
            return None;
        }
        Some(own_final(
            &module.name,
            &module.entry_aliased,
            &kept[from],
            name,
        ))
    };
    let written =
        |from: usize, name: &str| final_of(from, name).unwrap_or_else(|| name.to_string());
    // The traits each file can name, which a call of one of their functions
    // written without the trait is told from by: those the file defines and
    // those it lists.
    let mut scopes: Vec<(usize, Vec<String>)> = Vec::new();
    let entry_traits: Vec<String> = (file.traits.iter())
        .map(|t| t.name.clone())
        .chain(modules.iter().enumerate().flat_map(|(i, m)| {
            (m.file.traits.iter())
                .filter(|t| {
                    m.entry_imported.contains(&t.name)
                        || m.entry_aliased.iter().any(|(n, _)| *n == t.name)
                })
                .filter_map(|t| final_of(i, &t.name))
                .collect::<Vec<_>>()
        }))
        .collect();
    scopes.push((0, entry_traits));
    for (i, module) in modules.iter().enumerate() {
        let own = (module.file.traits.iter()).filter_map(|t| final_of(i, &t.name));
        let listed = module.imports.iter().filter_map(|i| {
            let t = modules[i.from]
                .file
                .traits
                .iter()
                .find(|t| t.name == i.name)?;
            Some(written(i.from, &t.name))
        });
        scopes.push((module.number, own.chain(listed).collect()));
    }
    // What each module writes of other modules' names, which is the same
    // for what is read now and for what is read later.
    struct Plan {
        imports: Vec<(String, String)>,
        through: HashMap<String, HashMap<String, String>>,
        rename: Option<HashMap<String, String>>,
    }
    let plans: Vec<Plan> = (0..modules.len())
        .map(|i| {
            // The modules it imports itself are written before a name of
            // their own, as `transaction.begin`, which is the name written
            // alone.
            let imports: Vec<(String, String)> = modules[i]
                .imports
                .iter()
                .map(|import| (import.written.clone(), written(import.from, &import.name)))
                .collect();
            let through: HashMap<String, HashMap<String, String>> = modules[i]
                .aliases
                .iter()
                .map(|(alias, from)| {
                    let names = all_names(&modules[*from])
                        .into_iter()
                        .filter(|name| final_of(*from, name).is_some())
                        .map(|name| (name.clone(), written(*from, &name)))
                        .collect();
                    (alias.clone(), names)
                })
                .collect();
            let rename = modules[i].pieces.is_none().then(|| {
                let mut rename: HashMap<String, String> = modules[i]
                    .defined
                    .iter()
                    .filter_map(|n| final_of(i, n).map(|to| (n.clone(), to)))
                    .filter(|(from, to)| from != to)
                    .collect();
                for (name, to) in &imports {
                    rename.insert(name.clone(), to.clone());
                }
                rename
            });
            Plan {
                imports,
                through,
                rename,
            }
        })
        .collect();
    let mut rest: Vec<LazyModule<'a>> = Vec::new();
    for (i, plan) in plans.into_iter().enumerate() {
        let standard = modules[i].name.starts_with("std.");
        let Plan {
            imports,
            through,
            rename,
        } = plan;
        if let Some(rename) = rename {
            let tokens = std::mem::take(&mut modules[i].tokens);
            let tokens = renamed_tokens(&tokens, &|n| rename.get(n).cloned(), &through);
            let found = match crate::parser::parse_library(&tokens) {
                Ok(found) => found,
                Err(found) => {
                    errors.extend(found);
                    continue;
                }
            };
            let held = if standard {
                modules[i].held.clone()
            } else {
                HashSet::new()
            };
            add_items(&mut file, found, &held);
            continue;
        }
        let module = &mut modules[i];
        let mut lazy = LazyModule {
            name: module.name.clone(),
            number: module.number,
            source: std::mem::take(&mut module.source),
            pieces: module.pieces.take().unwrap_or_else(|| unreachable!()),
            defined: std::mem::take(&mut module.defined).into_iter().collect(),
            held: std::mem::take(&mut module.held).into_iter().collect(),
            kept: kept[i].iter().cloned().collect(),
            aliased: module.entry_aliased.clone(),
            imports,
            through,
            parsed: Vec::new(),
        };
        lazy.parsed = (0..lazy.pieces.list.len()).map(|_| None).collect();
        // The traits, and what the compiler leans on, are read at once.
        for at in 0..lazy.pieces.list.len() {
            let piece = lazy.pieces.list[at];
            let text = &lazy.source[piece.name.0..piece.name.1];
            let now = match piece.kind {
                PieceKind::Trait => true,
                PieceKind::Struct | PieceKind::Enum => {
                    LEANED_ON.contains(&(lazy.name.as_str(), text))
                }
                _ => false,
            };
            if now
                && !lazy.held.contains(text)
                && let Some(found) = lazy.read(at, errors)
            {
                add_items(&mut file, found, &HashSet::new());
            }
        }
        rest.push(lazy);
    }
    file.scopes = scopes;
    let items = Items(
        (modules.iter().enumerate())
            .filter(|(_, module)| module.name.starts_with("std."))
            .map(|(i, module)| {
                (
                    module.name.clone(),
                    module.entry_aliased.clone(),
                    kept[i].clone(),
                )
            })
            .collect(),
    );
    (file, items, {
        let mut filter = Filter {
            bits: vec![0; 1024],
        };
        for module in &rest {
            module.mark(&mut filter);
        }
        Lazy {
            modules: rest,
            done: Set::default(),
            filter,
            written: Vec::new(),
        }
    })
}

/// Every name a module defines.
fn all_names(module: &Module) -> Vec<String> {
    let mut names: Vec<String> = module.defined.iter().cloned().collect();
    if let Some(p) = &module.pieces {
        names.extend(p.named.iter().map(|&i| {
            let piece = &p.list[i as usize];
            module.source[piece.name.0..piece.name.1].to_string()
        }));
    }
    names
}

/// The items of `found` among the file's, but for the names the compiler
/// builds, which are in `held`.
fn add_items(file: &mut File, found: File, held: &HashSet<String>) {
    let keep = |n: &str| !held.contains(n);
    file.functions
        .extend(found.functions.into_iter().filter(|f| keep(&f.name)));
    file.structs
        .extend(found.structs.into_iter().filter(|s| keep(&s.name)));
    file.enums
        .extend(found.enums.into_iter().filter(|e| keep(&e.name)));
    file.traits
        .extend(found.traits.into_iter().filter(|t| keep(&t.name)));
    file.impls.extend(found.impls);
    file.withouts.extend(found.withouts);
}

/// The modules of the standard library whose items are read as the program
/// reaches them.
struct Lazy<'a> {
    modules: Vec<LazyModule<'a>>,
    /// The names looked up, whose items are all read.
    done: Set<String>,
    /// What the names of the items not read may look like.
    filter: Filter,
    /// The names the file that is built writes.
    written: Vec<String>,
}

/// A set of the lengths and first letters that names have, so that a name
/// that is not one of a module's is told from one that may be without looking
/// it up.
struct Filter {
    bits: Vec<u64>,
}

impl Filter {
    fn at(len: usize, first: u8) -> (usize, u64) {
        let k = (len.min(255) << 8) | first as usize;
        (k / 64, 1 << (k % 64))
    }

    fn add(&mut self, name: &str, extra: usize) {
        let (word, bit) = Filter::at(
            name.len() + extra,
            name.as_bytes().first().copied().unwrap_or(0),
        );
        self.bits[word] |= bit;
    }

    fn maybe(&self, name: &str) -> bool {
        let (word, bit) = Filter::at(name.len(), name.as_bytes().first().copied().unwrap_or(0));
        self.bits[word] & bit != 0
    }
}

/// A module of the standard library whose items are read when they are
/// reached.
struct LazyModule<'a> {
    name: String,
    number: usize,
    source: std::borrow::Cow<'a, str>,
    pieces: Pieces,
    /// The names of what is read with the module.
    defined: Set<String>,
    /// The names the compiler builds, which are never read.
    held: Set<String>,
    /// The names the module writes as they are.
    kept: Set<String>,
    /// The names the file that is built gives the module's names with `as`.
    aliased: Vec<(String, String)>,
    /// What the module lists from other modules, as it writes each and as the
    /// file writes it.
    imports: Vec<(String, String)>,
    through: HashMap<String, HashMap<String, String>>,
    /// The items read since, by their place among the module's.
    parsed: Vec<Option<File>>,
}

impl LazyModule<'_> {
    /// Marks what the names of the items not read may look like: as the file
    /// writes them, with the module after them or as they are.
    fn mark(&self, filter: &mut Filter) {
        for p in &self.pieces.list {
            if matches!(p.kind, PieceKind::Fn | PieceKind::Struct | PieceKind::Enum) {
                let name = &self.source[p.name.0..p.name.1];
                filter.add(name, 1 + self.name.len());
            }
        }
        for name in &self.kept {
            filter.add(name, 0);
        }
        for (_, alias) in &self.aliased {
            filter.add(alias, 0);
        }
    }

    fn defines(&self, name: &str) -> bool {
        self.defined.contains(name)
            || !self
                .pieces
                .find(&self.source, &self.pieces.named, name)
                .is_empty()
    }

    /// What the module's name is written as in the file, when it is not as the
    /// module writes it.
    fn rename(&self, name: &str) -> Option<String> {
        if let Some((_, to)) = self.imports.iter().find(|(from, _)| from == name) {
            return Some(to.clone());
        }
        if !self.defines(name) || name.starts_with("__") {
            return None;
        }
        let to = own_final(&self.name, &self.aliased, &self.kept, name);
        (to != name).then_some(to)
    }

    /// Reads the item at the place among the module's, with its names written
    /// as the file writes them.
    fn read(&mut self, at: usize, errors: &mut Vec<Error>) -> Option<File> {
        self.pieces.loaded[at] = true;
        let piece = self.pieces.list[at];
        let text = &self.source[piece.text.0..piece.text.1];
        let tokens = crate::lexer::lex(text.as_bytes()).ok()?;
        let tokens = shifted(&tokens, self.number * STEP + piece.line);
        let tokens = renamed_tokens(&tokens, &|n| self.rename(n), &self.through);
        match crate::parser::parse_library(&tokens) {
            Ok(found) => Some(found),
            Err(found) => {
                errors.extend(found);
                None
            }
        }
    }

    /// The names, as the module writes them, that the name as the file writes
    /// it may be, among those the module defines, and among those it lists
    /// from other modules when `listed` says so.
    fn originals<'n>(&'n self, written: &'n str, listed: bool) -> Vec<&'n str> {
        let mut found: Vec<&str> = Vec::new();
        if let Some((name, module)) = written.split_once('@') {
            if module == self.name {
                found.push(name);
            }
        } else if self.kept.contains(written) {
            found.push(written);
        }
        found.extend(
            (self.aliased.iter())
                .filter(|(_, alias)| alias == written)
                .map(|(from, _)| from.as_str()),
        );
        if listed {
            found.extend(
                (self.imports.iter())
                    .filter(|(_, to)| to == written)
                    .map(|(from, _)| from.as_str()),
            );
        }
        found
    }

    /// The places of the items that are not read, which are not traits or
    /// `impl`s, that the file may write as `written`.
    fn provided(&self, written: &str) -> Vec<usize> {
        let mut out = Vec::new();
        match written.split_once('@') {
            Some((name, module)) => {
                if module == self.name {
                    self.provides(name, written, &mut out);
                }
            }
            None => {
                if self.kept.contains(written) {
                    self.provides(written, written, &mut out);
                }
            }
        }
        for (from, alias) in &self.aliased {
            if alias == written {
                self.provides(from, written, &mut out);
            }
        }
        out
    }

    /// The places of the items not read that the module writes as `name` and
    /// the file writes as `written`.
    fn provides(&self, name: &str, written: &str, out: &mut Vec<usize>) {
        if self.held.contains(name) {
            return;
        }
        for &i in self.pieces.find(&self.source, &self.pieces.named, name) {
            let i = i as usize;
            let kind = self.pieces.list[i].kind;
            if !self.pieces.loaded[i]
                && matches!(kind, PieceKind::Fn | PieceKind::Struct | PieceKind::Enum)
                && self.rename(name).as_deref().unwrap_or(name) == written
            {
                out.push(i);
            }
        }
    }

    /// Whether a struct or an enum that is not read is written as this.
    fn is_type(&self, written: &str) -> bool {
        self.originals(written, false).into_iter().any(|name| {
            (self
                .pieces
                .find(&self.source, &self.pieces.named, name)
                .iter())
            .any(|&i| {
                matches!(
                    self.pieces.list[i as usize].kind,
                    PieceKind::Struct | PieceKind::Enum
                )
            })
        })
    }

    /// The places of the `impl`s of the trait, written as `written`, that
    /// are not read and are for types that are all `reached`, except those
    /// the trait writes `out` for. `own` is the names the module writes of
    /// what is reached of its own, and `is_type` says whether a name that is
    /// not the module's own is a struct or an enum.
    fn impls_of(
        &self,
        written: &str,
        outs: &[bool],
        reached: &Set<&str>,
        own: &Set<&str>,
        loaded_type: &dyn Fn(&str) -> bool,
        lazy_type: &dyn Fn(&str) -> bool,
    ) -> Vec<usize> {
        let mut names: Vec<&str> = self.originals(written, true);
        // A trait the module does not define or list is written as it is.
        if !self.defines(written) && !self.imports.iter().any(|(from, _)| from == written) {
            names.push(written);
        }
        let mut out = Vec::new();
        for name in names {
            for &i in self.pieces.find(&self.source, &self.pieces.impls, name) {
                let i = i as usize;
                if self.pieces.loaded[i] {
                    continue;
                }
                let text = &self.source[self.pieces.list[i].text.0..self.pieces.list[i].text.1];
                let first = text.lines().next().unwrap_or("");
                let all = impl_arguments_all(first, name, |j, arg| {
                    outs.get(j).copied().unwrap_or(false)
                        || arg
                            .split(|c: char| !(c.is_alphanumeric() || c == '_'))
                            .filter(|w| !w.is_empty())
                            .all(|word| {
                                self.type_is_reached(word, reached, own, loaded_type, lazy_type)
                            })
                });
                if all {
                    out.push(i);
                }
            }
        }
        out
    }

    /// Whether a word in the head of an `impl` of the module is no struct or
    /// enum, or one that is reached.
    fn type_is_reached(
        &self,
        word: &str,
        reached: &Set<&str>,
        own: &Set<&str>,
        loaded_type: &dyn Fn(&str) -> bool,
        lazy_type: &dyn Fn(&str) -> bool,
    ) -> bool {
        if let Some((_, to)) = self.imports.iter().find(|(from, _)| from == word) {
            return !(loaded_type(to) || lazy_type(to)) || reached.contains(to.as_str());
        }
        if self.defined.contains(word) {
            let to = own_final(&self.name, &self.aliased, &self.kept, word);
            return !loaded_type(&to) || reached.contains(to.as_str());
        }
        let piece = self.pieces.find(&self.source, &self.pieces.named, word);
        if !piece.is_empty() {
            let is = piece.iter().any(|&i| {
                matches!(
                    self.pieces.list[i as usize].kind,
                    PieceKind::Struct | PieceKind::Enum
                )
            });
            if !is {
                return true;
            }
            return own.contains(word)
                || (self.kept.contains(word) && reached.contains(word))
                || self
                    .aliased
                    .iter()
                    .any(|(from, alias)| from == word && reached.contains(alias.as_str()));
        }
        !loaded_type(word) || reached.contains(word)
    }
}

/// Every name the items of a file look up, as `reach` does, but for those of
/// the prelude, which name nothing of a module, and those of the file that is
/// built, whose names the parser has.
fn names_in_items(file: &File, impls: &[Arc<Impl>], out: &mut dyn FnMut(&str)) {
    for f in file.functions.iter().filter(|f| of_library(f.pos)) {
        names_in_function(f, out);
    }
    for s in file.structs.iter().filter(|s| of_library(s.pos)) {
        for field in &s.fields {
            type_names(&field.ty).into_iter().for_each(|(n, _)| out(n));
        }
        bounds_named(&s.generics).into_iter().for_each(&mut *out);
        let defaults = s.fields.iter().filter_map(|f| f.default.as_ref());
        names_in_expressions(defaults.collect(), out);
    }
    for e in file.enums.iter().filter(|e| of_library(e.pos)) {
        for t in e.cases.iter().filter_map(|c| c.ty.as_ref()) {
            type_names(t).into_iter().for_each(|(n, _)| out(n));
        }
        bounds_named(&e.generics).into_iter().for_each(&mut *out);
    }
    for t in file.traits.iter().filter(|t| of_library(t.pos)) {
        for f in &t.functions {
            names_in_function(f, out);
        }
        bounds_named(&t.generics).into_iter().for_each(&mut *out);
    }
    for im in impls.iter().filter(|im| of_library(im.pos)) {
        out(&im.name);
        for t in &im.args {
            type_names(t).into_iter().for_each(|(n, _)| out(n));
        }
        bounds_named(&im.generics).into_iter().for_each(&mut *out);
        needs_named(&im.requires).into_iter().for_each(&mut *out);
        for f in &im.functions {
            names_in_function(f, out);
        }
    }
}

/// Every name a function looks up, as `reach` does.
fn names_in_function(f: &Function, out: &mut dyn FnMut(&str)) {
    types_named(f).into_iter().for_each(&mut *out);
    let defaults = f.params.iter().filter_map(|p| p.default.as_ref());
    let contracts = f.requires.iter().chain(&f.ensures);
    names_in_expressions(
        values(&f.body).chain(defaults).chain(contracts).collect(),
        out,
    );
    needs_named(&f.needs).into_iter().for_each(&mut *out);
}

/// Every name expressions look up, as `reach` does.
fn names_in_expressions(values: Vec<&Expr>, out: &mut dyn FnMut(&str)) {
    for e in exprs(values.iter().copied()) {
        match e {
            Expr::Field { of, .. } => {
                if let Expr::Name { name, .. } = &**of {
                    out(name);
                }
            }
            Expr::Name { name, .. } => out(name),
            Expr::Call(call) => match &call.qualifier {
                Some((q, _)) => out(q),
                None => out(&call.callee),
            },
            Expr::Loop(l) if l.over.is_some() => out("Iterate"),
            _ => {}
        }
    }
}

/// Whether `ok` holds of each argument of the trait in the first line of an
/// `impl`, as written, with its place among them.
fn impl_arguments_all(line: &str, name: &str, mut ok: impl FnMut(usize, &str) -> bool) -> bool {
    let Some(at) = line.find(name) else {
        return true;
    };
    let rest = &line[at + name.len()..];
    let Some(rest) = rest.strip_prefix('[') else {
        return true;
    };
    let mut depth = 0;
    let mut from = 0;
    let mut place = 0;
    for (i, c) in rest.char_indices() {
        match c {
            '[' | '(' => depth += 1,
            ']' | ')' if depth == 0 => return ok(place, &rest[from..i]),
            ']' | ')' => depth -= 1,
            ',' if depth == 0 => {
                if !ok(place, &rest[from..i]) {
                    return false;
                }
                place += 1;
                from = i + 1;
            }
            _ => {}
        }
    }
    true
}

impl<'a> Lazy<'a> {
    /// The places, among the items of the modules, of those that the program
    /// reaches and that are not read: those written by a name looked up, and
    /// the `impl`s of the traits reached for the types reached.
    fn wanted(&mut self, file: &File, names: &Names, reached: &Reached) -> Vec<(usize, usize)> {
        if self.modules.is_empty() {
            return Vec::new();
        }
        let mut traits: Set<&str> = Set::default();
        let mut types: Set<&str> = Set::default();
        for item in &reached.items {
            match *item {
                Item::Trait(k) => {
                    traits.insert(&file.traits[k].name);
                }
                Item::Struct(s) => {
                    types.insert(&file.structs[s].name);
                }
                Item::Enum(e) => {
                    types.insert(&file.enums[e].name);
                }
                Item::Fn(_) | Item::Impl(_) => {}
            }
        }
        let loaded_type = |n: &str| names.structs.contains_key(n) || names.enums.contains_key(n);
        let mut out: Vec<(usize, usize)> = Vec::new();
        for (m, module) in self.modules.iter().enumerate() {
            let own: Set<&str> = types
                .iter()
                .filter_map(|t| match t.split_once('@') {
                    Some((name, home)) if home == module.name => Some(name),
                    _ => None,
                })
                .collect();
            for &name in &traits {
                let outs: Vec<bool> = (names.traits.get(name))
                    .map(|&k| {
                        file.traits[k]
                            .generics
                            .iter()
                            .map(|g| g.out.is_some())
                            .collect()
                    })
                    .unwrap_or_default();
                out.extend(
                    module
                        .impls_of(name, &outs, &types, &own, &loaded_type, &|n| {
                            self.modules.iter().any(|m| m.is_type(n))
                        })
                        .into_iter()
                        .map(|i| (m, i)),
                );
            }
        }
        out.sort_unstable();
        out.dedup();
        out
    }

    /// Finds the items, not read, that a name calls for, once for each name.
    #[inline]
    fn note(&mut self, name: &str, work: &mut Vec<(usize, usize)>) {
        if !self.filter.maybe(name) || self.done.contains(name) {
            return;
        }
        for (m, module) in self.modules.iter().enumerate() {
            work.extend(module.provided(name).into_iter().map(|i| (m, i)));
        }
        self.done.insert(name.to_string());
    }

    /// Reads the items, which are not read yet, that the file names or that
    /// are `wanted`, and the items that the names in what is read call for,
    /// until there are no more. `file` is named by `names`, which is given
    /// every name it looks up.
    fn load(
        &mut self,
        wanted: &[(usize, usize)],
        written: &[String],
        names: impl FnOnce(&mut dyn FnMut(&str)),
        traits: &[String],
        errors: &mut Vec<Error>,
    ) {
        let mut work: Vec<(usize, usize)> = wanted.to_vec();
        for name in written {
            self.note(name, &mut work);
        }
        names(&mut |name| self.note(name, &mut work));
        while let Some((m, i)) = work.pop() {
            if self.modules[m].pieces.loaded[i] {
                continue;
            }
            let module = &mut self.modules[m];
            let Some(mut found) = module.read(i, errors) else {
                continue;
            };
            traits::split_functions_needs(&mut found.functions, traits);
            names_in_items(&found, &found.impls, &mut |name| self.note(name, &mut work));
            self.modules[m].parsed[i] = Some(found);
        }
    }

    /// Puts the items read among the file's, in the order of the modules and
    /// of their source.
    fn append(&self, file: &mut File) {
        for module in &self.modules {
            for found in module.parsed.iter().flatten() {
                file.functions.extend(found.functions.iter().cloned());
                file.structs.extend(found.structs.iter().cloned());
                file.enums.extend(found.enums.iter().cloned());
                file.impls.extend(found.impls.iter().cloned());
            }
        }
    }
}

/// An error for each `impl` that is neither in the module of its trait nor in
/// the module of one of the types it is for, which is a module that defines
/// one of them.
fn placement(file: &File, errors: &mut Vec<Error>) {
    let defined = defined_names(file);
    for im in &file.impls {
        let types = im.args.iter().flat_map(type_names);
        if !defined.contains(im.name.as_str())
            && !types.into_iter().any(|(n, _)| defined.contains(n))
        {
            errors.push(
                Error::new(
                    im.pos,
                    format!(
                        "this `impl` of `{}` is in a module that defines neither the trait nor a type it is for",
                        im.name
                    ),
                )
                .with_fix("move it to the module of the trait or of the type"),
            );
        }
    }
}

/// A warning for each definition `main` does not reach, at its name, and
/// for each binding whose name is never read, at the name.
fn warnings(file: &File, reached: &[Item]) -> Vec<Error> {
    let mut out = Vec::new();
    let reached: Set<Item> = reached.iter().copied().collect();
    let mut dead = |item: Item, pos: Pos, what: &dyn Fn() -> String| {
        if !reached.contains(&item) && !of_prelude(pos) && !of_library(pos) {
            let what = what();
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
        dead(Item::Fn(i), f.pos, &|| format!("the function `{}`", f.name));
    }
    for (i, s) in file.structs.iter().enumerate() {
        dead(Item::Struct(i), s.pos, &|| {
            format!("the struct `{}`", s.name)
        });
    }
    for (i, e) in file.enums.iter().enumerate() {
        dead(Item::Enum(i), e.pos, &|| format!("the enum `{}`", e.name));
    }
    for (i, t) in file.traits.iter().enumerate() {
        dead(Item::Trait(i), t.pos, &|| format!("the trait `{}`", t.name));
    }
    for (i, im) in file.impls.iter().enumerate() {
        let what = || {
            let args: Vec<String> = im.args.iter().map(written_type).collect();
            format!("the implementation `{}[{}]`", im.name, args.join(", "))
        };
        dead(Item::Impl(i), im.pos, &what);
    }
    let bodies =
        (file.functions.iter().map(|f| &**f)).chain(file.impls.iter().flat_map(|im| &im.functions));
    for f in bodies.filter(|f| !of_library(f.pos)) {
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
        Pattern::Number { .. } | Pattern::Text { .. } => Vec::new(),
    }
}

/// A struct or an enum, by its place among those of its kind.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Def {
    Struct(usize),
    Enum(usize),
}

/// The struct or the enum a type is, if it is one.
fn head(t: &Ty) -> Option<Def> {
    match t {
        Ty::Struct(s, _) => Some(Def::Struct(*s)),
        Ty::Enum(e, _) => Some(Def::Enum(*e)),
        _ => None,
    }
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
    fn names<'a>(t: &'a Type, out: &mut Vec<(&'a str, Pos)>) {
        if !t.name.is_empty() {
            out.push((t.name.as_str(), t.pos));
        }
        for a in &t.args {
            names(a, out);
        }
    }
    let mut out = Vec::new();
    names(t, &mut out);
    out
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
        Expr::Arith { left, right, .. } => {
            walk(left, out);
            walk(right, out);
        }
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
        Expr::Tuple { items, .. } | Expr::List { items, .. } => {
            for item in items {
                walk(item, out);
            }
        }
        Expr::Index { of, index, .. } => {
            walk(of, out);
            walk(index, out);
        }
        Expr::Slice { of, from, to, .. } => {
            walk(of, out);
            for end in [from, to].into_iter().flatten() {
                walk(end, out);
            }
        }
        Expr::Lambda(l) => {
            for value in values(&l.body) {
                walk(value, out);
            }
        }
        Expr::Loop(l) => {
            if let Some(r) = &l.range {
                walk(&r.from, out);
                walk(&r.to, out);
            }
            if let Some(o) = &l.over {
                walk(&o.value, out);
            }
            if let Some(e) = &l.decreases {
                walk(e, out);
            }
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
        Expr::Return { value, .. } => {
            if let Some(v) = value {
                walk(v, out);
            }
        }
        Expr::Text { .. }
        | Expr::Number { .. }
        | Expr::Unbuilt { .. }
        | Expr::Name { .. }
        | Expr::Break { .. } => {}
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
            Expr::Arith { left, right, .. } => {
                blocks(left, out);
                blocks(right, out);
            }
            Expr::Case {
                payload: Some(p), ..
            } => blocks(p, out),
            Expr::Match(m) => {
                blocks(&m.of, out);
                out.extend(m.arms.iter().map(|arm| arm.body.as_slice()));
            }
            Expr::Tuple { items, .. } | Expr::List { items, .. } => {
                for item in items {
                    blocks(item, out);
                }
            }
            Expr::Index { of, index, .. } => {
                blocks(of, out);
                blocks(index, out);
            }
            Expr::Slice { of, from, to, .. } => {
                blocks(of, out);
                for end in [from, to].into_iter().flatten() {
                    blocks(end, out);
                }
            }
            Expr::Lambda(l) => out.push(&l.body),
            Expr::Loop(l) => {
                if let Some(r) = &l.range {
                    blocks(&r.from, out);
                    blocks(&r.to, out);
                }
                if let Some(o) = &l.over {
                    blocks(&o.value, out);
                }
                if let Some(e) = &l.decreases {
                    blocks(e, out);
                }
                out.push(&l.body);
            }
            Expr::Interp { parts, .. } => {
                for part in parts {
                    if let Part::Expr(e) = part {
                        blocks(e, out);
                    }
                }
            }
            Expr::Return { value: Some(v), .. } => blocks(v, out),
            Expr::Case { .. }
            | Expr::Text { .. }
            | Expr::Number { .. }
            | Expr::Unbuilt { .. }
            | Expr::Name { .. }
            | Expr::Break { .. }
            | Expr::Return { value: None, .. } => {}
        }
    }
    fn collect<'a>(body: &'a [Stmt], out: &mut Vec<&'a Stmt>) {
        for (stmt, value) in body.iter().zip(values(body)) {
            out.push(stmt);
            let mut found = Vec::new();
            blocks(value, &mut found);
            for block in found {
                collect(block, out);
            }
        }
    }
    let mut out = Vec::with_capacity(16);
    collect(body, &mut out);
    out
}

/// Every expression in the expressions, in order, each before the
/// expressions inside it, and then from left to right.
fn exprs<'a>(exprs: impl IntoIterator<Item = &'a Expr>) -> Vec<&'a Expr> {
    let mut inside = Vec::with_capacity(32);
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
    out.extend(needs_named(&f.needs));
    out
}

/// A function with a body: one written on its own, the function of an
/// implementation that implements the trait's function at a place, or the
/// one function of an implementation a type is given.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Hash)]
enum Template {
    Free(usize),
    Impl(usize, usize),
    /// The implementation of a trait a type is given automatically, by the
    /// trait's place. Its copy's one type is the type.
    Given(usize),
}

/// A copy of a function, for a choice of its type parameters.
// Equality is the derived one, with a count of its calls, so it agrees with `Hash`.
#[allow(clippy::derived_hash_with_manual_eq)]
#[derive(Clone, Debug, Eq, Hash)]
struct Instance {
    template: Template,
    args: Vec<Ty>,
}

impl PartialEq for Instance {
    fn eq(&self, other: &Instance) -> bool {
        crate::work::instance();
        self.template == other.template && self.args == other.args
    }
}

/// A function with a body, as its body sees it: its signature is the
/// trait's, for the function of an implementation.
struct View<'a> {
    f: &'a Function,
    scope: &'a Scope,
    params: Vec<(String, Ty)>,
    gives: Ty,
    /// How its result type is written, if it has one.
    result: Option<Written<'a>>,
    /// The effects it lists.
    effects: &'a [(String, Pos)],
}

/// What a checked call does, once every type in it is known.
enum Callee {
    Print,
    /// `__eprint`, which the prelude calls to write a line to standard error.
    Eprint,
    /// `__end`, which the prelude calls to end the program with a status.
    End,
    Construct(usize, Vec<Ty>),
    /// A case of an enum written in full, by its place among the enum's
    /// cases.
    Case(usize),
    /// A copy of a function to run, and what it gives.
    Run(Instance, Ty),
    /// The function a name holds, which a parameter or a lambda's parameter
    /// was given.
    Value,
    /// A function the compiler builds, which only the prelude calls.
    Intrinsic,
    /// `push`, which gives a list with one more element.
    Push,
    /// `freeze`, which gives the memory of a list as a slice.
    Freeze,
    /// `utf8`, which gives a text of the bytes, or where they stop being valid.
    Utf8,
    /// `narrow` or one of its three relatives, which take a whole number to
    /// a type that does not hold all of its values.
    Narrow(Narrowing),
}

/// For each argument of a call, the place of its parameter, or none when an
/// argument is for no parameter: a name that is not one, or more arguments by
/// position than there are parameters. Such a call is reported where its
/// arguments are checked, so a call that was not reported gives some.
fn params_of(params: &[(String, Ty)], args: &[Arg]) -> Option<Vec<usize>> {
    let mut next = 0;
    (args.iter())
        .map(|arg| match &arg.name {
            Some((name, _)) => params.iter().position(|(p, _)| p == name),
            None if arg.after => params.len().checked_sub(1),
            None => {
                next += 1;
                (next <= params.len()).then(|| next - 1)
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
                match traits::trait_function(&file.traits[k], name) {
                    Some(fi) => (params(&file.traits[k].functions[fi]), true),
                    None => return Vec::new(),
                }
            }
            None => {
                if let Some(&s) = self.struct_names.get(name) {
                    let fields = (file.structs[s].fields.iter())
                        .map(|f| (f.name.as_str(), f.default.as_ref()))
                        .collect();
                    (fields, false)
                } else if name == "print" || is_stream_call(call) {
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

    /// The parameters of a function with a body, among its type
    /// parameters, which those of a copy of it replace.
    fn params(&self, t: Template) -> Vec<(String, Ty)> {
        match t {
            Template::Given(k) => self.trait_sigs[k][0].params.clone(),
            t => self.view(t).params,
        }
    }

    fn view(&self, t: Template) -> View<'_> {
        let file = self.file;
        match t {
            Template::Given(_) => unreachable!("a given implementation has no body"),
            Template::Free(f) => {
                let sig = &self.signatures[f];
                let def = &file.functions[f];
                View {
                    f: def,
                    scope: &sig.scope,
                    params: sig.params.clone(),
                    gives: sig.gives.clone(),
                    result: def.result.as_ref().map(Written::Source),
                    effects: &def.effects,
                }
            }
            Template::Impl(i, fi) => {
                let info = self.impls[i]
                    .as_ref()
                    .expect("an implementation that is known");
                let sig = &self.trait_sigs[info.of][fi];
                let scope = &info.fn_scopes[fi];
                // The function's own type parameters stand for themselves.
                let mut env = info.args.clone();
                let n = info.scope.names.len();
                env.extend((n..scope.names.len()).map(Ty::Param));
                let gives = sig.gives.subst(&env);
                let f = &file.impls[i].functions[info.fns[fi].expect("implemented")];
                View {
                    f,
                    scope,
                    // The names the implementation wrote, so that a renamed
                    // parameter is reported once, where it is renamed.
                    params: (f.params.iter().enumerate())
                        .map(|(j, p)| {
                            let ty = sig
                                .params
                                .get(j)
                                .map_or(Ty::Unknown, |(_, t)| t.subst(&env));
                            (p.name.clone(), ty)
                        })
                        .collect(),
                    result: (gives != Ty::Nothing).then_some(Written::Rendered),
                    gives,
                    effects: &file.traits[info.of].functions[fi].effects,
                }
            }
        }
    }
}

/// The address of a node of the syntax tree, by which its type is recorded.
/// Whether a call is of a function the compiler builds, whose name starts
/// with two underscores and which only the prelude calls.
fn is_intrinsic(call: &Call) -> bool {
    call.qualifier.is_none()
        && call.callee.starts_with("__")
        && !lists_and_text::is_library_call(call)
        && (of_prelude(call.pos) || of_std(call.pos))
}

/// Whether a call is of the function `name`, which the compiler builds for
/// the prelude and no other file may call.
fn is_prelude_call(call: &Call, name: &str) -> bool {
    call.qualifier.is_none() && call.callee == name && of_prelude(call.pos)
}

/// Whether a call is of `__eprint` or `__end`, which only the prelude calls.
fn is_stream_call(call: &Call) -> bool {
    is_prelude_call(call, "__eprint") || is_prelude_call(call, "__end")
}

fn addr<T>(node: &T) -> usize {
    node as *const T as usize
}

/// The definitions, with the type of each expression checked, among the
/// type parameters of the definition it is written in.
struct Typed<'a> {
    defs: &'a Defs<'a>,
    types: HashMap<usize, Ty>,
    /// The expressions that give a range where a wider one is needed, by
    /// their addresses, with the wider range each is taken as.
    widened: HashMap<usize, Int>,
    /// The calls of a name that holds a function, by their addresses.
    value_calls: Set<usize>,
    /// The names of functions passed as values, by their addresses, with
    /// the function each names.
    fn_values: Map<usize, usize>,
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
        let given = |i: usize| {
            let arg = &call.args[i].value;
            match self.widened.get(&addr(arg)) {
                Some(wide) => Ty::Int(*wide),
                None => self.type_of(arg, env),
            }
        };
        let solve = |params: &[(String, Ty)], vars: usize| {
            let mut sol = vec![None; vars];
            let places = params_of(params, &call.args).expect("checked");
            for (i, p) in places.into_iter().enumerate() {
                unify(&params[p].1, &as_read(&given(i), &params[p].1), &mut sol);
            }
            sol.into_iter()
                .map(|t| t.unwrap_or(Ty::Unknown))
                .collect::<Vec<Ty>>()
        };
        // A type that nothing settled is its default, if it has one.
        let defaulted = |mut sol: Vec<Ty>, defaults: Vec<Option<Ty>>| {
            for (i, d) in defaults.iter().enumerate() {
                if let (Ty::Unknown, Some(d)) = (&sol[i], d) {
                    sol[i] = d.subst(&sol);
                }
            }
            sol
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
                if name == "__eprint" {
                    return Callee::Eprint;
                }
                if name == "__end" {
                    return Callee::End;
                }
                if is_intrinsic(call) {
                    return Callee::Intrinsic;
                }
                if is_push(defs, call) {
                    return Callee::Push;
                }
                if is_freeze(defs, call) {
                    return Callee::Freeze;
                }
                if is_utf8(defs, call) {
                    return Callee::Utf8;
                }
                if let Some(narrowing) = narrowing(defs, call) {
                    return Callee::Narrow(narrowing);
                }
                if let Some(&f) = defs.function_names.get(name) {
                    let sig = &defs.signatures[f];
                    let args = solve(&sig.params, sig.scope.names.len());
                    let args = defaulted(args, sig.param_defaults.clone());
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
                let at = traits::trait_function(&defs.file.traits[k], &call.callee);
                (k, at.expect("checked"))
            }
        };
        let sig = &defs.trait_sigs[k][fi];
        let types = solve(&sig.params, sig.scope.names.len());
        let mut types = self.result_types(call, sig, env, types);
        // The function's own type parameters follow the trait's.
        let own = types.split_off(defs.file.traits[k].generics.len());
        let (instance, mut all) = defs.chosen(k, fi, types, own.clone());
        all.extend(own);
        Callee::Run(instance, sig.gives.subst(&all))
    }

    /// The copy of the `each` that a `loop` over a value runs, where `env`
    /// stands for the type parameters of the definition it is written in.
    fn each_of(&self, o: &Over, env: &[Ty]) -> Instance {
        let walked = as_read(&self.type_of(&o.value, env), &Ty::List(vec![Ty::Unknown]));
        let defs = self.defs;
        defs.chosen(defs.iterate, 0, vec![walked, Ty::Unknown], Vec::new())
            .0
    }

    /// The types that stand for the type parameters of the defaults a call
    /// leaves out: those of the function, of the trait, or of the struct
    /// they are written in.
    fn env_of(&self, callee: &Callee) -> Vec<Ty> {
        match callee {
            Callee::Run(instance, _) => match instance.template {
                Template::Free(_) | Template::Given(_) => instance.args.clone(),
                Template::Impl(i, _) => {
                    let info = self.defs.impls[i].as_ref().expect("known");
                    info.args.iter().map(|t| t.subst(&instance.args)).collect()
                }
            },
            Callee::Construct(_, args) => args.clone(),
            Callee::Print
            | Callee::Eprint
            | Callee::End
            | Callee::Case(_)
            | Callee::Value
            | Callee::Intrinsic
            | Callee::Push
            | Callee::Freeze
            | Callee::Utf8
            | Callee::Narrow(_) => Vec::new(),
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
    /// A given implementation has no body, so its calls are at `at`, where
    /// it is run.
    fn calls_of(&self, instance: &Instance, at: Pos) -> (Vec<(Pos, Instance)>, Vec<Pos>) {
        if let Template::Given(k) = instance.template {
            let calls = self.defs.given_calls(k, &instance.args);
            return (calls.into_iter().map(|c| (at, c)).collect(), Vec::new());
        }
        let view = self.defs.view(instance.template);
        let mut out = Vec::new();
        let mut cycles = Vec::new();
        for e in exprs(values(&view.f.body)) {
            if let Some(&f) = self.fn_values.get(&addr(e)) {
                let template = Template::Free(f);
                let args = Vec::new();
                out.push((e.pos(), Instance { template, args }));
            }
            if let Expr::Loop(l) = e
                && let Some(o) = &l.over
            {
                out.push((l.pos, self.each_of(o, &instance.args)));
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
    /// walked from `main`, by their templates, `done` those already walked,
    /// and `at` where the copy is run.
    ///
    /// A given implementation runs only those of its parts, which are
    /// smaller, and of its own type, for another trait, so a copy that would
    /// grow forever passes through a function written in the source, where
    /// it is reported.
    fn copies(
        &self,
        instance: &Instance,
        at: Pos,
        path: &mut Map<Template, Vec<Instance>>,
        done: &mut Set<Instance>,
        errors: &mut Vec<Error>,
    ) {
        path.entry(instance.template)
            .or_default()
            .push(instance.clone());
        let (calls, cycles) = self.calls_of(instance, at);
        errors.extend(cycles.into_iter().map(default_cycle));
        for (pos, callee) in calls {
            let size = |i: &Instance| i.args.iter().map(Ty::size).sum::<usize>();
            let grows = |p: &Instance| {
                !matches!(callee.template, Template::Given(_)) && size(&callee) > size(p)
            };
            let endless = (path.get(&callee.template).into_iter().flatten())
                .any(|p| *p == callee || grows(p));
            if endless {
                errors.push(Error::unsupported(
                    pos,
                    "a function that calls itself, directly or through others,",
                ));
            } else if !done.contains(&callee) {
                self.copies(&callee, pos, path, done, errors);
            }
        }
        if let Some(walking) = path.get_mut(&instance.template) {
            walking.pop();
        }
        done.insert(instance.clone());
    }
}

/// How the result type of a function is said when it is wrong: as it is written
/// in the source, or as the type of the trait's function it implements.
enum Written<'a> {
    Source(&'a Type),
    Rendered,
}

impl Written<'_> {
    fn text(&self, view: &View, defs: &Defs) -> String {
        match self {
            Written::Source(t) => written_type(t),
            Written::Rendered => defs.text(&view.gives, view.scope),
        }
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
    /// The expressions that give a range where a wider one is needed, by
    /// their addresses, with the wider range each is taken as.
    widened: HashMap<usize, Int>,
    /// Whether the arguments being fitted are those of a comparison, whose
    /// two sides are taken as the smallest range that holds both.
    comparing: bool,
    /// Whether the type a part of a case is fitted to was settled by the type
    /// that was asked for, which a range that is narrower is taken to.
    settled: bool,
    /// The calls of a name that holds a function, by their addresses.
    value_calls: Set<usize>,
    /// The calls of `narrow`, by their addresses, which give the number
    /// they are given, as `arith` knows.
    narrowed: HashSet<usize>,
    /// The names of functions passed as values, by their addresses, with
    /// the function each names.
    fn_values: Map<usize, usize>,
    /// Whether the call of a trait's function being checked has types no
    /// implementation is for, which is already reported.
    no_impl: bool,
    /// How many lambdas after a call have been reported as given to a
    /// function that cannot take them, so that a call that had one is not
    /// reported again for what it gives.
    refusals: usize,
    /// While a number literal is checked against the type the uses of its
    /// name decided, the place of the use that decided it.
    deciding: Option<Pos>,
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
///
/// `Int` matches the whole number equal to it, and `Text` the text equal to
/// it.
enum Pat {
    Any,
    Case(usize, Option<Box<Pat>>),
    Tuple(Vec<Pat>),
    Int(i128),
    Text(Vec<u8>),
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
                Pat::Case(..) | Pat::Tuple(_) | Pat::Int(_) | Pat::Text(_) => return None,
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
                        // The cases of `bool` are written as bare names.
                        None if *e == defs.boolean => (case.name.clone(), &left[..]),
                        None => (format!(".{}", case.name), &left[..]),
                    };
                    let mut row = vec![first];
                    row.extend_from_slice(after);
                    out.push(row);
                }
            }
            out
        }
        // No list of literals covers every number or every text.
        Ty::Int(_) | Ty::Flex(_) | Ty::Text => {
            let rows: Rows = (rows.iter())
                .filter(|r| matches!(r[0], Pat::Any))
                .map(|r| r[1..].to_vec())
                .collect();
            (missing_rows(defs, &rows, rest).into_iter())
                .map(|left| {
                    let mut row = vec!["_".to_string()];
                    row.extend(left);
                    row
                })
                .collect()
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
        (_, Ty::Int(_) | Ty::Flex(_) | Ty::Text) => {
            // A literal is matched again by the same literal and by a bare
            // name, and a bare name only by a bare name.
            let same = |p: &Pat| match (row[0], p) {
                (_, Pat::Any) => true,
                (Pat::Int(a), Pat::Int(b)) => a == b,
                (Pat::Text(a), Pat::Text(b)) => a == b,
                _ => false,
            };
            let rows: Rows = (rows.iter())
                .filter(|r| same(r[0]))
                .map(|r| r[1..].to_vec())
                .collect();
            useful_row(defs, &rows, &row[1..], rest)
        }
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
    /// What looking at the uses of the whole numbers bound to names has
    /// found, or is finding.
    numbers: Numbers,
    /// The names of numbers with no type written that the uses decided, with
    /// the type and the place of the use that decided it.
    decided: HashMap<String, (Int, Option<Pos>)>,
    /// What `arith` knows where the body is being checked.
    facts: proof::Facts,
    /// The places of the expressions the body can end with, which each have
    /// to meet what the function `ensures`.
    tails: Vec<usize>,
    /// The type of the result of the function.
    gives: Ty,
    /// Whether the body is a contract, which states what is so of whole
    /// numbers and never computes, so that nothing in it can overflow.
    contract: bool,
    /// How many `loop`s the code being checked is inside, in the function or
    /// lambda it is written in.
    loops: usize,
    /// How many `loop`s the lambdas around the code being checked are inside,
    /// which a `break` cannot reach.
    outer_loops: usize,
    /// The type a `return` in the code being checked gives: the result of
    /// the function or the lambda it is written in, which a lambda whose
    /// result is not yet known leaves unknown.
    returns: Ty,
    /// The type of the first `return` in a lambda whose result is not yet
    /// known, which is then the result.
    returned: Option<Ty>,
    /// How many lambdas the code being checked is inside.
    lambdas: usize,
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
    /// What only `main` must be: with no parameter, or with one, a list of
    /// the words the program was started with.
    fn main(&mut self, index: usize) {
        let f = &self.defs.file.functions[index];
        if let Some(g) = f.generics.first() {
            self.errors
                .push(Error::unsupported(g.pos, "a type parameter of `main`"));
        }
        let signature = &self.defs.signatures[index];
        let words = Ty::list(Ty::Text);
        for (i, p) in f.params.iter().enumerate() {
            let right = i == 0 && signature.params[i].1 == words && p.default.is_none();
            if !right && signature.params[i].1 != Ty::Unknown {
                self.errors.push(
                    Error::new(
                        p.pos,
                        "`main` takes no parameter, or one, the words the program was started with, as a `List[str]`",
                    )
                    .with_fix("write `fn main(args: List[str])`, or take the parameter away"),
                );
            }
        }
        if let Some(t) = &f.result {
            let gives = &signature.gives;
            let exit = self.defs.exit;
            if *gives != Ty::Unknown
                && let Found::No = self
                    .defs
                    .meets(exit, gives, &signature.scope, &mut Vec::new())
            {
                self.errors.push(
                    Error::new(
                        t.pos,
                        format!(
                            "`main` returns {}, which does not implement `Exit`, the trait that gives the status the program ends with",
                            self.defs.describe(gives, &signature.scope)
                        ),
                    )
                    .with_fix("return `()`, or a `Result` of it, or write `impl Exit` for the type"),
                );
            }
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
            } else if !of_prelude(g.pos)
                && !of_library(g.pos)
                && (name == TEXT
                    || defs.struct_names.contains_key(name)
                    || defs.enum_names.contains_key(name)
                    || defs.trait_names.contains_key(name))
            {
                self.errors.push(Error::new(
                    g.pos,
                    format!("`{name}` is already the name of a type"),
                ));
            }
            match &g.default {
                Some(d) => {
                    let scope = defs.scope(generics, &[]);
                    for (n, at) in type_names(d) {
                        if let Some(later) = generics[i..].iter().find(|h| h.name == n) {
                            self.errors.push(Error::new(
                                at,
                                format!(
                                    "the default of `{name}` names `{}`, which does not come before it",
                                    later.name
                                ),
                            ));
                        }
                    }
                    self.ty(d, &scope);
                }
                None => {
                    if let Some(with) = generics[..i].iter().find(|h| h.default.is_some()) {
                        self.errors.push(Error::new(
                            g.pos,
                            format!(
                                "`{name}` has no default, and comes after `{}`, which has one",
                                with.name
                            ),
                        ));
                    }
                }
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
            numbers: Numbers::Off,
            decided: HashMap::new(),
            facts: proof::Facts::default(),
            tails: Vec::new(),
            gives: Ty::Nothing,
            contract: false,
            loops: 0,
            outer_loops: 0,
            returns: Ty::Nothing,
            returned: None,
            lambdas: 0,
        };
        let gives = self.expr(&mut body, value, &Want::Ty(needed.clone()));
        self.expect(&mut body, value, &gives, needed);
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
        defs.requirements(&def.needs, &defs.signatures[f].scope, &mut self.errors);
        self.params(def, &defs.signatures[f].scope);
        self.defaults(def, &defs.signatures[f]);
        self.contracts(def, &defs.signatures[f]);
        self.effects(&def.effects);
    }

    /// Reports a value that gives `given` where `needed` is needed.
    fn expect(&mut self, body: &mut Body<'a>, value: &Expr, given: &Ty, needed: &Ty) {
        let defs = self.defs;
        match (given, needed) {
            (_, Ty::Unknown) | (Ty::Unknown, _) => {}
            (g, n)
                if {
                    use_at(value.pos());
                    flex_pair(g, n)
                } => {}
            (Ty::Nothing, needed) if needed != given => {
                let needed = defs.describe(needed, body.scope);
                self.errors.push(self.nothing_where(value, &needed));
            }
            // A range is taken where a wider one is needed, with nothing written.
            (Ty::Int(g), Ty::Int(n)) if n.holds(*g) => {
                if g != n {
                    self.widened.insert(addr(value), *n);
                }
            }
            // A range is taken where a narrower one is needed only where it
            // is shown to fit.
            (Ty::Int(g), Ty::Int(n)) => {
                self.widened.insert(addr(value), *n);
                self.enters(body, value, (*g, *n));
            }
            (given, needed) if given != needed => {
                let message = self.mismatch(
                    body,
                    value,
                    defs.describe(given, body.scope),
                    defs.describe(needed, body.scope),
                );
                self.errors.push(Error::new(value.pos(), message));
            }
            _ => {}
        }
    }

    /// Reports a value that gives `given` where a value of `pattern` is
    /// needed, whose type parameters, those of `vars`, stand for the types
    /// in `sol`, and fills in `sol`.
    fn fit(
        &mut self,
        body: &mut Body<'a>,
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
            // A range is taken where a wider one is needed, with nothing written.
            (Ty::Int(g), Ty::Int(n)) if n.holds(*g) => {
                if g != n {
                    self.widened.insert(addr(value), *n);
                }
            }
            (Ty::Int(g), Ty::Int(n)) => {
                self.widened.insert(addr(value), *n);
                self.enters(body, value, (*g, *n));
            }
            // A range is taken where a type that is a range is already settled,
            // as the item of an `Option[Byte]` that is asked for, as it is taken
            // where that range is written.
            (Ty::Int(g), Ty::Param(p)) if self.settled && matches!(&sol[*p], Some(Ty::Int(_))) => {
                let Some(Ty::Int(n)) = sol[*p].clone() else {
                    unreachable!("checked")
                };
                if n.holds(*g) {
                    if *g != n {
                        self.widened.insert(addr(value), n);
                    }
                } else {
                    self.widened.insert(addr(value), n);
                    self.enters(body, value, (*g, n));
                }
            }
            // The other side of a comparison is a range too.
            (Ty::Int(g), Ty::Param(p))
                if self.comparing
                    && matches!(&sol[*p], Some(Ty::Int(x)) if x != g && x.hull(*g).is_some()) =>
            {
                let Some(Ty::Int(x)) = &sol[*p] else {
                    unreachable!("checked")
                };
                sol[*p] = x.hull(*g).map(Ty::Int);
            }
            (given, pattern) => {
                use_at(value.pos());
                // What is needed is said as it is before the types are matched,
                // and only when they do not match.
                let before = sol.to_vec();
                let read = as_read(given, pattern);
                if !unify(pattern, &read, sol) {
                    let needed = defs.describe_pattern(pattern, &before, vars, body.scope);
                    let message = match given {
                        Ty::Text if read != *given => format!(
                            "this gives text, which is a `List[u8]`, where {needed} is needed"
                        ),
                        _ => self.mismatch(body, value, defs.describe(given, body.scope), needed),
                    };
                    self.errors.push(Error::new(value.pos(), message));
                }
            }
        }
    }

    /// What is wrong with `value`, which gives `given` where `needed` is
    /// needed. A name bound to a number whose type a use decided says which.
    fn mismatch(&self, body: &Body, value: &Expr, given: String, needed: String) -> String {
        if let Expr::Name { name, .. } = value
            && let Some((int, Some(at))) = body.decided.get(name)
        {
            return format!(
                "`{name}` is `{}`, as the use at {}:{} needs, where {needed} is needed",
                int.name(),
                at.line,
                at.col
            );
        }
        format!("this gives {given}, where {needed} is needed")
    }

    fn nothing_where(&self, value: &Expr, needed: &str) -> Error {
        let what = match value {
            Expr::Call(call) => format!("`{}` returns nothing", call.callee),
            _ => "this gives nothing".to_string(),
        };
        Error::new(value.pos(), format!("{what}, where {needed} is needed"))
    }

    /// Checks the body of a function. A whole number bound to a name with no
    /// type written takes the type its uses need, so the body is first looked
    /// at with each such number undecided, to find what its uses ask of it,
    /// and then checked, with each decided and the mistakes reported.
    fn body(&mut self, template: Template) {
        USES.with(|u| *u.borrow_mut() = Uses::default());
        let (errors, refusals, no_impl) = (self.errors.len(), self.refusals, self.no_impl);
        let places = self.run_body(template, Numbers::Finding(Vec::new()));
        self.errors.truncate(errors);
        self.refusals = refusals;
        self.no_impl = no_impl;
        let uses = USES.with(|u| std::mem::take(&mut *u.borrow_mut()));
        // The numbers that must be of one type are decided together.
        let mut root: Vec<usize> = (0..places.len()).collect();
        fn find(root: &mut [usize], k: usize) -> usize {
            if root[k] != k {
                let r = find(root, root[k]);
                root[k] = r;
            }
            root[k]
        }
        for (j, k) in &uses.same {
            let (j, k) = (find(&mut root, *j), find(&mut root, *k));
            root[j.max(k)] = j.min(k);
        }
        let mut decided = HashMap::new();
        for (k, pos) in places.iter().enumerate() {
            let r = find(&mut root, k);
            let first = (uses.wants.iter())
                .filter(|(w, ..)| find(&mut root, *w) == r)
                .min_by_key(|(.., at)| *at);
            let decision = match first {
                Some((_, int, at)) => (*int, Some(*at)),
                None => (Int::I64, None),
            };
            decided.insert(*pos, decision);
        }
        self.run_body(template, Numbers::Decided(decided));
        if self.errors.len() == errors {
            let view = self.defs.view(template);
            ownership::check(
                self.defs,
                &self.types,
                (view.f, view.scope, &view.params),
                &mut self.errors,
            );
        }
    }

    /// One look at the body of a function, which gives the places of the
    /// literals of the numbers it bound to names with no type written.
    fn run_body(&mut self, template: Template, numbers: Numbers) -> Vec<Pos> {
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
            numbers,
            decided: HashMap::new(),
            facts: proof::Facts::default(),
            tails: Vec::new(),
            gives: view.gives.clone(),
            contract: false,
            loops: 0,
            outer_loops: 0,
            returns: view.gives.clone(),
            returned: None,
            lambdas: 0,
        };
        if let Template::Free(_) = template {
            for requirement in &f.requires {
                self.assume(&mut body, requirement, true);
            }
            if !f.ensures.is_empty() {
                proof::tails(&f.body, &mut body.tails);
            }
        } else if let Some(contract) = f.requires.first().or(f.ensures.first()) {
            self.errors.push(Error::unsupported(
                contract.pos(),
                "a contract of an implementation",
            ));
        }
        let want = match view.result {
            Some(_) => Want::Ty(view.gives.clone()),
            None => Want::Unused,
        };
        match (self.block(&mut body, &f.body, &want), &view.result) {
            _ if proof::ends_in_exit(&f.body) => {}
            (End::Empty | End::Binding(_), Some(_)) if view.gives == Ty::Nothing => {}
            (End::Empty, Some(t)) => {
                let t = t.text(&view, self.defs);
                self.errors.push(Error::new(
                    f.end,
                    format!(
                        "`{}` returns `{t}`, and its body is empty, which gives nothing",
                        f.name
                    ),
                ))
            }
            (End::Binding(pos), Some(t)) => {
                let t = t.text(&view, self.defs);
                self.ends_with_nothing(f, pos, &t)
            }
            (End::Value(gives, value), Some(_)) => {
                self.expect(&mut body, value, &gives, &view.gives)
            }
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
        match body.numbers {
            Numbers::Finding(places) => places,
            _ => Vec::new(),
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
            let first_return = stmts
                .iter()
                .position(|s| matches!(s, Stmt::Expr(Expr::Return { .. })));
            if first_return.is_some_and(|r| r + 1 == i) {
                let at = match stmt {
                    Stmt::Define { pos, .. } | Stmt::Assign { pos, .. } => *pos,
                    Stmt::Destructure { pattern, .. } => pattern.pos(),
                    Stmt::Expr(value) => value.pos(),
                };
                self.errors.push(Error::new(
                    at,
                    "this line never runs, as the `return` before it leaves the function or lambda",
                ));
            }
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
                                self.assign(body, at, value);
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
                                None if (matches!(value, Expr::Number { .. })
                                    || matches!(value, Expr::Match(m) if literal_blocks(m)))
                                    && !matches!(body.numbers, Numbers::Off) =>
                                {
                                    self.literal_binding(body, name, value)
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
                                    self.track_var(body, value);
                                } else {
                                    self.assume_binding(body, value);
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
                    if let Some(at) = body.names.iter().position(|(n, _)| n == name) {
                        self.assign_field(body, at);
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
                    let held = self.assigned_vars(body, &[std::slice::from_ref(stmt)]);
                    self.forget(body, &held);
                    End::Binding(pattern.pos())
                }
                Stmt::Expr(value) => {
                    let want = if is_last { last } else { &Want::Unused };
                    let gives = self.expr(body, value, want);
                    if !is_last && self.defs.is_result(&gives) {
                        self.dropped(body, value, &gives);
                    }
                    End::Value(gives, value)
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
                        Some(at) => {
                            if hidden(def, &def.fields[at], *pos) {
                                self.errors.push(private_field(def, field, *pos));
                            }
                            defs.fields[s][at].subst(&args)
                        }
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

    /// Checks a `return`: what it gives is the result of the function or the
    /// lambda around it, and a `return` with no value is where that gives
    /// nothing.
    fn returning(&mut self, body: &mut Body<'a>, value: Option<&Expr>, pos: Pos) {
        let defs = self.defs;
        let needed = body.returns.clone();
        match value {
            None => {
                if needed != Ty::Nothing && !needed.unknown() {
                    let needed = defs.describe(&needed, body.scope);
                    self.errors.push(
                        Error::new(
                            pos,
                            format!("this `return` has no value, where {needed} is needed"),
                        )
                        .with_fix("write the value after `return`"),
                    );
                }
            }
            Some(value) => {
                let want = match &needed {
                    Ty::Nothing => Want::Unused,
                    t if t.unknown() => Want::Any,
                    t => Want::Ty(t.clone()),
                };
                let gives = self.expr(body, value, &want);
                match (&needed, &body.returned) {
                    (Ty::Nothing, _) => {
                        if !matches!(gives, Ty::Nothing | Ty::Unknown) {
                            let given = defs.describe(&gives, body.scope);
                            self.errors.push(Error::new(
                                value.pos(),
                                format!("this `return` gives {given}, where nothing is needed"),
                            ));
                        }
                    }
                    (t, Some(first)) if t.unknown() => {
                        let first = first.clone();
                        self.expect(body, value, &gives, &first);
                    }
                    (t, None) if t.unknown() => body.returned = Some(gives),
                    (t, _) => self.expect(body, value, &gives, t),
                }
                if body.lambdas == 0 && !body.tails.is_empty() {
                    self.ensure(body, value);
                }
            }
        }
    }

    /// Checks an expression against what is wanted of it, and gives its
    /// type, which is recorded.
    fn expr(&mut self, body: &mut Body<'a>, expr: &Expr, want: &Want) -> Ty {
        let ty = self.expr_of(body, expr, want);
        self.types.insert(addr(expr), ty.clone());
        if let Expr::Call(call) = expr {
            self.types.insert(addr(call), ty.clone());
        }
        if body.tails.contains(&addr(expr)) {
            self.ensure(body, expr);
        }
        ty
    }

    fn expr_of(&mut self, body: &mut Body<'a>, expr: &Expr, want: &Want) -> Ty {
        let defs = self.defs;
        match expr {
            Expr::Unbuilt { .. } => Ty::Unknown,
            Expr::Text { .. } => Ty::Text,
            Expr::Number {
                magnitude,
                negative,
                pos,
                ..
            } => self.number(body, expr, *magnitude, *negative, *pos, want),
            Expr::Interp { pos, parts } => self.interpolation(body, *pos, parts),
            Expr::Name { name, pos } => {
                if let Some((_, ty)) = body.names.iter().rfind(|(n, _)| n == name) {
                    ty.clone()
                } else if let (Some(&f), None) =
                    (defs.function_names.get(name.as_str()), &body.default)
                {
                    self.function_value(body, expr, f, *pos)
                } else if name == "true" || name == "false" {
                    defs.bool_ty()
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
            Expr::Arith {
                op,
                left,
                right,
                op_pos,
            } => self.arith(body, expr, (*op, left, right, *op_pos), want),
            Expr::List { pos, items } => self.list_literal(body, *pos, items, want),
            Expr::Index { of, index, pos } => self.index(body, of, index, *pos),
            Expr::Slice { of, from, to, pos } => {
                self.piece(body, of, (from.as_deref(), to.as_deref()), *pos)
            }
            Expr::Field { of, name, pos } => {
                if let Some(e) = defs.qualified_case(of, &body.names) {
                    return self.case(body, Some(e), name, of.pos(), *pos, None, want);
                }
                match self.expr(body, of, &Want::Any) {
                    Ty::List(_) | Ty::Slice(_) | Ty::Text if name == "size" => Ty::Int(Int::U64),
                    Ty::Text if name == "bytes" => Ty::slice(Ty::Int(Int::U8)),
                    Ty::List(_) | Ty::Slice(_) => {
                        self.errors.push(Error::new(
                            *pos,
                            format!("a list has no field named `{name}`"),
                        ));
                        Ty::Unknown
                    }
                    Ty::Struct(s, args) => {
                        let def = &defs.file.structs[s];
                        match def.fields.iter().position(|f| &f.name == name) {
                            Some(at) => {
                                if hidden(def, &def.fields[at], *pos) {
                                    self.errors.push(private_field(def, name, *pos));
                                }
                                defs.fields[s][at].subst(&args)
                            }
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
                Want::Ty(Ty::Fn(items)) => self.lambda(body, l, items, false),
                Want::Ty(Ty::Unknown) => {
                    let unknown = vec![Ty::Unknown; l.params.len() + 1];
                    self.lambda(body, l, &unknown, true);
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
            Expr::Loop(l) => self.loop_expr(body, l),
            Expr::Break { pos } => {
                if body.loops == 0 {
                    let message = if body.outer_loops == 0 {
                        "`break` is outside a `loop`, so there is nothing for it to end"
                    } else {
                        "`break` is inside a lambda, which cannot end a `loop` outside it"
                    };
                    self.errors.push(Error::new(*pos, message));
                }
                Ty::Nothing
            }
            Expr::Return { value, pos } => {
                self.returning(body, value.as_deref(), *pos);
                Ty::Nothing
            }
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
                let mut given: Vec<Ty> = (items.iter().zip(&wants))
                    .map(|(item, want)| self.expr(body, item, want))
                    .collect();
                // A range is taken as an element where the element the tuple is
                // asked for is a range, as it is taken anywhere a type is needed.
                for ((item, got), want) in items.iter().zip(&mut given).zip(&wants) {
                    if let (Ty::Int(g), Want::Ty(Ty::Int(n))) = (&*got, want) {
                        let (g, n) = (*g, *n);
                        if g != n {
                            self.widened.insert(addr(item), n);
                            if !n.holds(g) {
                                self.enters(body, item, (g, n));
                            }
                            *got = Ty::Int(n);
                        }
                    }
                }
                Ty::tuple(given)
            }
        }
    }

    /// Settles the types of lambda `l` among those `wanted` of it, the
    /// result last, which are unknown where what it is passed to leaves them
    /// open: a type it writes takes the place of one that is open, and one
    /// that is not the type wanted is an error at the type.
    fn written_types(&mut self, body: &mut Body<'a>, l: &Lambda, wanted: &mut [Ty]) {
        let defs = self.defs;
        let written = l.types.iter().chain(std::iter::once(&l.result));
        for (slot, t) in wanted.iter_mut().zip(written) {
            let Some(t) = t else { continue };
            let ty = self.ty(t, body.scope);
            if ty == Ty::Unknown {
                continue;
            }
            if agrees(slot, &ty) {
                *slot = ty;
            } else {
                self.errors.push(Error::new(
                    t.pos,
                    format!(
                        "this is {}, where the function type needs {}",
                        defs.describe(&ty, body.scope),
                        defs.describe(slot, body.scope)
                    ),
                ));
            }
        }
    }

    /// Checks a lambda passed where a function is needed whose parameters
    /// and result, the result last, are of the types `wanted`, which may be
    /// partly known, and gives its type. Its body is checked where it is
    /// written, against the effects of the function it is written in, and
    /// it may assign that function's `var`s.
    fn lambda(&mut self, body: &mut Body<'a>, l: &Lambda, wanted: &[Ty], quiet: bool) -> Ty {
        let defs = self.defs;
        let params = wanted.len() - 1;
        let mut wanted = wanted.to_vec();
        if l.params.len() == params {
            self.written_types(body, l, &mut wanted);
        }
        let wanted = &wanted[..];
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
        let known = body.facts.mark();
        let assigned = self.assigned_vars(body, &[&l.body]);
        self.forget_all(body);
        let loops = body.loops;
        body.outer_loops += std::mem::take(&mut body.loops);
        let mut types = Vec::new();
        for (i, ((name, pos), ty)) in l.params.iter().zip(params).enumerate() {
            let twice = l.params[..i].iter().any(|(n, _)| n == name)
                || body.names.iter().any(|(n, _)| n == name);
            if twice {
                self.errors.push(self.defined_twice(body, name, *pos));
            }
            types.push(ty.clone());
            // The ownership check reads the type of a parameter from here.
            self.types.insert(addr(&l.params[i]), ty.clone());
            body.names.push((name.clone(), ty.clone()));
        }
        let want = match result {
            Ty::Nothing => Want::Unused,
            Ty::Unknown if quiet => Want::Ty(Ty::Unknown),
            t if t.unknown() => Want::Any,
            t => Want::Ty(t.clone()),
        };
        let outer_returns = std::mem::replace(&mut body.returns, result.clone());
        let outer_returned = body.returned.take();
        body.lambdas += 1;
        let end = self.block(body, &l.body, &want);
        body.lambdas -= 1;
        body.returns = outer_returns;
        let returned = std::mem::replace(&mut body.returned, outer_returned);
        let result = match returned {
            Some(t) if result.unknown() => &t.clone(),
            _ => result,
        };
        let gives = match (end, result) {
            _ if proof::ends_in_exit(&l.body) => result.clone(),
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
        body.facts.reset(&known);
        self.forget(body, &assigned);
        body.outer_loops -= loops;
        body.loops = loops;
        types.push(gives);
        Ty::func(types)
    }

    /// Checks the name of function `f`, at `pos`, passed as a value, and
    /// gives its type. A function type states no effect, so the effects of
    /// `f` are required of the function the name is written in, as those
    /// of a lambda that calls it are.
    fn function_value(&mut self, body: &mut Body<'a>, expr: &Expr, f: usize, pos: Pos) -> Ty {
        let defs = self.defs;
        let sig = &defs.signatures[f];
        let name = &defs.file.functions[f].name;
        let error = if !sig.scope.names.is_empty() {
            Error::unsupported(pos, "passing a function with type parameters as a value")
        } else if sig.params.iter().any(|(_, t)| t.holds_fn()) {
            Error::unsupported(pos, "passing a function that takes a function as a value")
        } else if !defs.file.functions[f].requires.is_empty() {
            Error::new(
                pos,
                format!(
                    "`{name}` has a `requires`, which a function type does not state, so it is \
                     only called by its name"
                ),
            )
        } else {
            for effect in &sig.effects {
                self.needs(body, effect, pos, &format!("`{name}`"));
            }
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
                self.settled = true;
                self.fit(body, p, &gives, pattern, &mut sol, vars);
                self.settled = false;
            }
            (Some(pattern), None) if pattern.solved(&sol) == Ty::Nothing => {}
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
        match m.form {
            Form::Match | Form::Try => {}
            Form::If { otherwise } => return self.if_expr(body, m, otherwise, want),
            Form::Not | Form::And | Form::Or => return self.logic(body, m),
        }
        // What `try` splits is what the function it is written in gives
        // when it leaves.
        let split = match m.form {
            Form::Try => Want::Ty(Ty::Enum(defs.flow, vec![Ty::Unknown, body.returns.clone()])),
            _ => Want::Any,
        };
        let of = self.expr(body, &m.of, &split);
        let outer = body.names.len();
        let vars = body.vars.len();
        let known = body.facts.mark();
        let blocks: Vec<&[Stmt]> = m.arms.iter().map(|a| a.body.as_slice()).collect();
        let assigned = self.assigned_vars(body, &blocks);
        let (mut paths, mut excluded) = (Vec::new(), false);
        let mut patterns: Vec<Option<Pat>> = Vec::new();
        let mut first: Option<Ty> = None;
        let mut later = Vec::new();
        let mut ends = Vec::new();
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
            self.arm(
                body,
                arm,
                want,
                (&mut first, &mut later, &mut ends),
                "a `match`",
            );
            if proof::ends_in_exit(&arm.body) {
                excluded = true;
            } else {
                paths.push(self.leave_arm(body, &known, &assigned));
            }
            body.names.truncate(outer);
            body.vars.truncate(vars);
            body.facts.reset(&known);
        }
        self.settle(body, later, &mut first);
        self.join(body, outer, paths, &assigned, excluded);
        if !of.unknown() && patterns.iter().all(Option::is_some) {
            let all: Vec<&Pat> = patterns.iter().flatten().collect();
            let left = missing(defs, &all, &of);
            if left == ["_"] {
                self.errors.push(
                    Error::new(m.pos, "this `match` does not cover the rest of the values")
                        .with_fix("end with a bare name that covers the rest"),
                );
            } else if !left.is_empty() {
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

    /// Checks the block of an arm of a `match`, or of an `if`, which an
    /// error names as `what`, against what is wanted of the whole or what
    /// the first block gives, which is `first` once known.
    ///
    /// Where nothing says the type yet, a block that ends in a literal does
    /// not decide it: the literal waits in `later` for the type of a block
    /// that follows, which `settle` gives it.
    fn arm<'e>(
        &mut self,
        body: &mut Body<'a>,
        arm: &'e Arm,
        want: &Want,
        (first, later, ends): (&mut Option<Ty>, &mut Vec<&'e Expr>, &mut Vec<(usize, Int)>),
        what: &str,
    ) {
        if let (Want::Any, None, Some((Stmt::Expr(tail), rest))) =
            (want, &*first, arm.body.split_last())
            && flexible(tail)
            && !proof::ends_in_exit(&arm.body)
        {
            self.block(body, rest, &Want::Unused);
            later.push(tail);
            return;
        }
        let arm_want = match (want, &*first) {
            (Want::Unused, _) => Want::Unused,
            (Want::Ty(t), _) => Want::Ty(t.clone()),
            // A block that gives a range may give another, so only a number
            // written out waits for the type of the first.
            (Want::Any, Some(Ty::Int(_))) if !matches!(arm.body.last(), Some(Stmt::Expr(e)) if flexible(e)) => {
                Want::Any
            }
            (Want::Any, Some(t)) => Want::Ty(t.clone()),
            (Want::Any, None) => Want::Any,
        };
        let end = self.block(body, &arm.body, &arm_want);
        // With no type needed, the blocks that give ranges give the smallest
        // range that holds them all, and each is taken as it.
        if let (Want::Any, End::Value(Ty::Int(g), value), false) =
            (want, &end, proof::ends_in_exit(&arm.body))
        {
            let held = match &*first {
                None => Some(*g),
                Some(Ty::Int(f)) => f.hull(*g),
                Some(_) => None,
            };
            if let Some(h) = held {
                ends.push((addr(*value), *g));
                for (at, int) in ends.iter() {
                    if *int == h {
                        self.widened.remove(at);
                    } else {
                        self.widened.insert(*at, h);
                    }
                }
                *first = Some(Ty::Int(h));
                return;
            }
        }
        if !matches!(want, Want::Unused) {
            let gives = self.arm_gives(body, arm, end, want, first, what);
            if !proof::ends_in_exit(&arm.body) {
                first.get_or_insert(gives);
            }
        }
    }

    /// Gives the blocks that ended in a literal, which waited for a type,
    /// the type of the first block that gave one, or an `i64` when none did.
    fn settle(&mut self, body: &mut Body<'a>, later: Vec<&Expr>, first: &mut Option<Ty>) {
        if later.is_empty() {
            return;
        }
        let ty = first.clone().unwrap_or(Ty::Int(Int::I64));
        for value in later {
            self.expr(body, value, &Want::Ty(ty.clone()));
        }
        first.get_or_insert(ty);
    }

    /// Checks an `if`: its condition, which is a `bool`, and its blocks.
    /// Without `else`, it gives `()`, and what its block gives is not read,
    /// unless it is a `Result`, whose failure must not be dropped.
    fn if_expr(&mut self, body: &mut Body<'a>, m: &Match, otherwise: bool, want: &Want) -> Ty {
        self.condition(body, &m.of);
        let outer = body.names.len();
        let vars = body.vars.len();
        let known = body.facts.mark();
        let blocks: Vec<&[Stmt]> = m.arms.iter().map(|a| a.body.as_slice()).collect();
        let assigned = self.assigned_vars(body, &blocks);
        let (mut paths, mut excluded) = (Vec::new(), false);
        if !otherwise {
            let arm = &m.arms[0];
            self.assume(body, &m.of, true);
            if let End::Value(gives, value) = self.block(body, &arm.body, &Want::Unused)
                && self.defs.is_result(&gives)
            {
                self.dropped(body, value, &gives);
            }
            if proof::ends_in_exit(&arm.body) {
                excluded = true;
            } else {
                paths.push(self.leave_arm(body, &known, &assigned));
            }
            body.names.truncate(outer);
            body.vars.truncate(vars);
            body.facts.reset(&known);
            // Where the condition is not so, nothing runs.
            self.assume(body, &m.of, false);
            paths.push(self.leave_arm(body, &known, &assigned));
            body.facts.reset(&known);
            self.join(body, outer, paths, &assigned, excluded);
            return Ty::Nothing;
        }
        let mut first = None;
        let mut later = Vec::new();
        let mut ends = Vec::new();
        for (k, arm) in m.arms.iter().enumerate() {
            self.assume(body, &m.of, k == 0);
            self.arm(
                body,
                arm,
                want,
                (&mut first, &mut later, &mut ends),
                "an `if`",
            );
            if proof::ends_in_exit(&arm.body) {
                excluded = true;
            } else {
                paths.push(self.leave_arm(body, &known, &assigned));
            }
            body.names.truncate(outer);
            body.vars.truncate(vars);
            body.facts.reset(&known);
        }
        self.settle(body, later, &mut first);
        self.join(body, outer, paths, &assigned, excluded);
        match want {
            Want::Unused => Ty::Nothing,
            Want::Ty(t) if !t.unknown() => t.clone(),
            _ => first.unwrap_or(Ty::Nothing),
        }
    }

    /// Checks a `loop`, which gives nothing. Over a range, its body runs once
    /// for each whole number from the first end up to but not including the
    /// second, and `arith` knows that in it. Over a list, it runs once for
    /// each element, in order. Open-ended, it runs until a `break`, and its
    /// function carries `diverge`.
    fn loop_expr(&mut self, body: &mut Body<'a>, l: &Loop) -> Ty {
        let counter = match &l.range {
            Some(r) => Some((r, self.range_type(body, r))),
            None => {
                if l.decreases.is_none() && l.over.is_none() {
                    self.needs(body, "diverge", l.pos, "an open-ended `loop`");
                }
                None
            }
        };
        let element = l.over.as_ref().map(|o| (o, self.element_type(body, o)));
        let measure = l.decreases.as_ref();
        if let Some(e) = measure {
            let gives = self.expr(body, e, &Want::Any);
            if !matches!(gives, Ty::Int(t) if !t.signed())
                && !matches!(gives, Ty::Unknown | Ty::Flex(_))
            {
                let given = self.defs.describe(&gives, body.scope);
                self.errors.push(Error::new(
                    e.pos(),
                    format!(
                        "`decreases` is a whole number of an unsigned type, and this is {given}"
                    ),
                ));
            }
        }
        let assigned = self.assigned_vars(body, &[&l.body]);
        let outer = body.names.len();
        let vars = body.vars.len();
        let known = body.facts.mark();
        // The ends of a range are worked out once, when the loop starts, so
        // what they say is about the values before any round begins.
        if let Some((r, ty)) = counter {
            if body.names.iter().any(|(n, _)| n == &r.name) {
                self.errors.push(self.defined_twice(body, &r.name, r.pos));
            }
            body.names.push((r.name.clone(), ty));
            self.assume_range(body, r);
        }
        if let Some((o, ty)) = element {
            if body.names.iter().any(|(n, _)| n == &o.name) {
                self.errors.push(self.defined_twice(body, &o.name, o.pos));
            }
            body.names.push((o.name.clone(), ty));
        }
        self.begin_round(body, &assigned);
        let start = measure.and_then(|e| self.measure(body, e));
        body.loops += 1;
        if let End::Value(gives, value) = self.block(body, &l.body, &Want::Unused)
            && self.defs.is_result(&gives)
        {
            self.dropped(body, value, &gives);
        }
        body.loops -= 1;
        if let (Some(e), Some(start)) = (measure, &start) {
            self.decreased(body, l, e, start);
        }
        body.names.truncate(outer);
        body.vars.truncate(vars);
        body.facts.reset(&known);
        self.forget(body, &assigned);
        Ty::Nothing
    }

    /// Checks the value a `loop` runs over, which is of a type that
    /// implements `Iterate`, and gives the type of its elements. A text is
    /// read as its bytes. The value is worked out once, when the loop starts,
    /// so assigning a `var` it was read from changes nothing about which
    /// elements the loop gives.
    fn element_type(&mut self, body: &mut Body<'a>, o: &Over) -> Ty {
        let defs = self.defs;
        let given = self.expr(body, &o.value, &Want::Any);
        if given.unknown() {
            return Ty::Unknown;
        }
        let walked = as_read(&given, &Ty::List(vec![Ty::Unknown]));
        match defs.find(
            defs.iterate,
            &[walked.clone(), Ty::Unknown],
            body.scope,
            &mut Vec::new(),
        ) {
            Found::Yes(all, _) => all[1].clone(),
            Found::Unknown => Ty::Unknown,
            Found::Two(a, b) => {
                self.errors.push(defs.ambiguous(o.value.pos(), a, b));
                Ty::Unknown
            }
            Found::No => {
                self.errors.push(Error::new(
                    o.value.pos(),
                    format!(
                        "there is no implementation of `Iterate` for {}, so a `loop` cannot \
                         run over it",
                        defs.describe(&given, body.scope)
                    ),
                ));
                Ty::Unknown
            }
        }
    }

    /// Checks the two ends of a range, which are whole numbers of one type,
    /// and gives that type, which each number of the range has.
    fn range_type(&mut self, body: &mut Body<'a>, r: &Range) -> Ty {
        let defs = self.defs;
        // Two literals are one number, whose type the uses of the counter
        // decide, as for a literal bound to a name.
        if flexible(&r.from) && flexible(&r.to) {
            match &mut body.numbers {
                Numbers::Off => {}
                Numbers::Finding(places) => {
                    places.push(r.from.pos());
                    let at = places.len() - 1;
                    self.expr(body, &r.from, &Want::Ty(Ty::Unknown));
                    self.expr(body, &r.to, &Want::Ty(Ty::Unknown));
                    return Ty::Flex(at);
                }
                Numbers::Decided(decided) => {
                    let (int, by) = decided[&r.from.pos()];
                    self.deciding = by;
                    self.expr(body, &r.from, &Want::Ty(Ty::Int(int)));
                    self.expr(body, &r.to, &Want::Ty(Ty::Int(int)));
                    self.deciding = None;
                    body.decided.insert(r.name.clone(), (int, by));
                    return Ty::Int(int);
                }
            }
        }
        let (l, e) = match (flexible(&r.from), flexible(&r.to)) {
            (true, false) => {
                let e = self.expr(body, &r.to, &Want::Any);
                let side = Self::side_want(&e);
                (self.expr(body, &r.from, &side), e)
            }
            (false, true) => {
                let l = self.expr(body, &r.from, &Want::Any);
                let side = Self::side_want(&l);
                let e = self.expr(body, &r.to, &side);
                (l, e)
            }
            (_, _) => (
                self.expr(body, &r.from, &Want::Any),
                self.expr(body, &r.to, &Want::Any),
            ),
        };
        match (&l, &e) {
            (Ty::Unknown, _) | (_, Ty::Unknown) => Ty::Unknown,
            (Ty::Int(a), Ty::Int(b)) if a == b => l.clone(),
            (Ty::Int(a), Ty::Int(b)) => {
                self.errors.push(Error::new(
                    r.to.pos(),
                    format!(
                        "the two ends of a range need to be of one type, and the first is `{}` \
                         where the second is `{}`",
                        a.name(),
                        b.name()
                    ),
                ));
                Ty::Unknown
            }
            (Ty::Flex(_) | Ty::Int(_), Ty::Flex(_) | Ty::Int(_)) => {
                use_at(r.pos);
                if !flex_pair(&l, &e) {
                    unreachable!("a number and a number")
                }
                if matches!(l, Ty::Flex(_)) && matches!(e, Ty::Flex(_)) {
                    l.clone()
                } else if matches!(l, Ty::Flex(_)) {
                    e.clone()
                } else {
                    l.clone()
                }
            }
            (a, b) => {
                let other = if matches!(a, Ty::Int(_) | Ty::Flex(_)) {
                    b
                } else {
                    a
                };
                let given = defs.describe(other, body.scope);
                self.errors.push(Error::new(
                    r.from.pos(),
                    format!(
                        "a `loop` runs over whole numbers, and an end of this range is {given}"
                    ),
                ));
                Ty::Unknown
            }
        }
    }

    /// Checks `not`, `and`, or `or`, each of whose sides is a `bool`, and
    /// which gives a `bool`.
    fn logic(&mut self, body: &mut Body<'a>, m: &Match) -> Ty {
        let truth = self.defs.bool_ty();
        self.condition(body, &m.of);
        let known = body.facts.mark();
        let blocks: Vec<&[Stmt]> = m.arms.iter().map(|a| a.body.as_slice()).collect();
        let assigned = self.assigned_vars(body, &blocks);
        for (k, arm) in m.arms.iter().enumerate() {
            self.assume(body, &m.of, k == 0);
            if let End::Value(gives, value) = self.block(body, &arm.body, &Want::Ty(truth.clone()))
            {
                self.expect(body, value, &gives, &truth);
            }
            body.facts.reset(&known);
        }
        self.forget(body, &assigned);
        truth
    }

    /// Checks a value that must be a `bool`.
    fn condition(&mut self, body: &mut Body<'a>, value: &Expr) {
        let truth = self.defs.bool_ty();
        let gives = self.expr(body, value, &Want::Ty(truth.clone()));
        self.expect(body, value, &gives, &truth);
    }

    /// Reports a `Result` that a statement gives and nothing uses.
    fn dropped(&mut self, body: &Body, value: &Expr, gives: &Ty) {
        self.errors.push(
            Error::new(
                value.pos(),
                format!(
                    "this gives {}, which nothing uses, so a failure would be dropped",
                    self.defs.describe(gives, body.scope)
                ),
            )
            .with_fix("write `try` before it, or match on it"),
        );
    }

    /// Checks what the block of an arm gives, where the `match` or the `if`
    /// is used as an expression, against what is wanted of it or what the
    /// first block gives, and gives it.
    fn arm_gives(
        &mut self,
        body: &mut Body<'a>,
        arm: &Arm,
        end: End,
        want: &Want,
        first: &Option<Ty>,
        what: &str,
    ) -> Ty {
        let defs = self.defs;
        let needed = match (want, first) {
            (Want::Ty(t), _) => Some(t),
            (_, Some(t)) => Some(t),
            _ => None,
        };
        let describe = |t: &Ty| defs.describe(t, body.scope);
        if proof::ends_in_exit(&arm.body) {
            return Ty::Unknown;
        }
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
        // A range is taken where a wider one is wanted, with nothing written.
        if let (Want::Ty(Ty::Int(n)), Ty::Int(g), End::Value(_, value)) = (want, &gives, &end) {
            self.widened.insert(addr(*value), *n);
            if !n.holds(*g) {
                self.enters(body, value, (*g, *n));
            }
            return gives;
        }
        let gives_what = match (&end, &gives) {
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
            Want::Ty(t) => format!("{gives_what}, where {} is needed", describe(t)),
            _ => format!(
                "{gives_what}, and the first block gives {}, where the blocks of {what} give \
                 the same type",
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
                            self.errors.push(Error::new(
                                *pos,
                                format!("this pattern names `{name}` twice"),
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
            Pattern::Number {
                text,
                magnitude,
                negative,
                pos,
            } => {
                if assigned.is_some() {
                    self.errors
                        .push(Error::unsupported(*pos, "a literal in a binding's pattern"));
                    return None;
                }
                let literal = Expr::Number {
                    text: text.clone(),
                    magnitude: *magnitude,
                    negative: *negative,
                    pos: *pos,
                };
                match ty {
                    Ty::Unknown => None,
                    Ty::Int(_) | Ty::Flex(_) => {
                        let errors = self.errors.len();
                        self.number(
                            body,
                            &literal,
                            *magnitude,
                            *negative,
                            *pos,
                            &Want::Ty(ty.clone()),
                        );
                        if self.errors.len() > errors {
                            return None;
                        }
                        let value = i128::try_from(*magnitude).ok()?;
                        Some(Pat::Int(if *negative { -value } else { value }))
                    }
                    other => {
                        self.errors.push(Error::new(
                            *pos,
                            format!(
                                "this is a number, and this matches {}",
                                defs.describe(other, body.scope)
                            ),
                        ));
                        None
                    }
                }
            }
            Pattern::Text { text, pos } => {
                if assigned.is_some() {
                    self.errors
                        .push(Error::unsupported(*pos, "a literal in a binding's pattern"));
                    return None;
                }
                match ty {
                    Ty::Unknown => None,
                    Ty::Text => Some(Pat::Text(text.clone())),
                    other => {
                        self.errors.push(Error::new(
                            *pos,
                            format!(
                                "this is a text, and this matches {}",
                                defs.describe(other, body.scope)
                            ),
                        ));
                        None
                    }
                }
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
                    (Some(holds), None) if holds.subst(args) == Ty::Nothing => {
                        Some(Pat::Case(k, Some(Box::new(Pat::Any))))
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
                        .with_fix("to write a name after a module, import the module with `use`")
                });
                return self.unknown_call(body, call);
            };
            let t = &defs.file.traits[k];
            let Some(fi) = traits::trait_function(t, name) else {
                self.errors.push(Error::new(
                    call.name_pos,
                    format!("`{q}` has no function named `{name}`"),
                ));
                return self.unknown_call(body, call);
            };
            return self.trait_call(body, call, k, fi, want);
        }
        if let Some((_, Ty::Fn(items))) = body.names.iter().rfind(|(n, _)| n == name) {
            let items = items.clone();
            return self.value_call(body, call, &items);
        }
        if let Some(&s) = defs.struct_names.get(name) {
            return self.construction(body, call, s, want);
        }
        if is_prelude_call(call, "__eprint") {
            let print = Signature::print();
            self.arguments(body, call, &print, true, None, None);
            return Ty::Nothing;
        }
        if is_prelude_call(call, "__end") {
            let end = Signature::end();
            self.arguments(body, call, &end, true, None, None);
            return Ty::Nothing;
        }
        if is_prelude_call(call, "__convert") {
            return self.convert_intrinsic(body, call, want);
        }
        if is_intrinsic(call) {
            return self.intrinsic(body, call);
        }
        if is_push(defs, call) {
            return self.push(body, call, want);
        }
        if is_freeze(defs, call) {
            return self.freeze(body, call, want);
        }
        if is_utf8(defs, call) {
            return self.utf8(body, call);
        }
        if let Some(narrowing) = narrowing(defs, call) {
            return self.narrow(body, call, narrowing, want);
        }
        if name == "print" {
            let print = Signature::print();
            if self.effect_in_default(body, call, &print) {
                return self.unknown_call(body, call);
            }
            self.arguments(body, call, &print, true, None, None);
            return Ty::Nothing;
        }
        if let Some(&f) = defs.function_names.get(name) {
            let sig = &defs.signatures[f];
            if self.effect_in_default(body, call, sig) {
                return self.unknown_call(body, call);
            }
            let refused = self.refusals;
            let placed = match want {
                Want::Ty(t) => Some(t),
                _ => None,
            };
            let mut sol = self.arguments(body, call, sig, false, None, placed);
            for (i, d) in sig.param_defaults.iter().enumerate() {
                if let (None, Some(d)) = (&sol[i], d) {
                    let ty = d.solved(&sol);
                    sol[i] = (!ty.unknown()).then_some(ty);
                }
            }
            if self.refusals != refused || self.function_for_parameter(&sol, call.pos) {
                return Ty::Unknown;
            }
            for r in &sig.scope.requires {
                let types: Vec<Ty> = r.types.iter().map(|t| t.solved(&sol)).collect();
                if types.contains(&Ty::Unknown) {
                    continue;
                }
                match defs.meets_types(r.of, &types, body.scope, &mut Vec::new()) {
                    Found::No => {
                        let because = match types.as_slice() {
                            [t] => defs.because(r.of, t, body.scope),
                            _ => String::new(),
                        };
                        self.errors.push(Error::new(
                            call.pos,
                            format!(
                                "there is no implementation of `{}` for {}, which `{name}` \
                                 needs{because}",
                                defs.file.traits[r.of].name,
                                defs.types_of(r.of, &types, body.scope)
                            ),
                        ));
                    }
                    Found::Two(a, b) => self.errors.push(defs.ambiguous(call.pos, a, b)),
                    Found::Yes(..) | Found::Unknown => {}
                }
            }
            self.contract_call(body, call, &defs.file.functions[f], sig);
            return sig.gives.solved(&sol);
        }
        let mut of = defs.trait_fns.get(name).cloned().unwrap_or_default();
        if let [(k, _)] = of[..] {
            of.extend(defs.rivals(k, name, call.pos));
        }
        match of.as_slice() {
            [(k, fi)] => self.trait_call(body, call, *k, *fi, want),
            of if !of.is_empty() => {
                let traits: Vec<&str> = (of.iter())
                    .map(|(k, _)| written_name(&defs.file.traits[*k].name))
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

    /// Before a lambda is checked against the function a trait's function
    /// takes, gives the types that are not yet known but which the
    /// implementation the known ones choose fixes: its `out` types, and the
    /// type the call gives when the call's value is what is wanted. Gives
    /// whether an implementation was not found, which is reported.
    fn completed(
        &mut self,
        body: &Body,
        call: &Call,
        (k, want): (usize, &Want),
        sig: &Signature,
        sol: &mut [Option<Ty>],
    ) -> bool {
        let defs = self.defs;
        let outs = &defs.outs[k];
        // The type the call gives is that of what the lambda gives, so it
        // may be known only from what is wanted.
        let gives = match &sig.gives {
            Ty::Param(r) if !outs[*r] => Some(*r),
            _ => None,
        };
        if let (Some(r), Want::Ty(wanted)) = (gives, want)
            && sol[r].is_none()
            && !wanted.unknown()
        {
            sol[r] = Some(wanted.clone());
        }
        if (sol.iter().zip(outs).enumerate())
            .any(|(i, (t, out))| !out && t.is_none() && Some(i) != gives)
        {
            return false;
        }
        let types: Vec<Ty> = sol
            .iter()
            .map(|t| t.as_ref().map_or(Ty::Unknown, Ty::concrete))
            .collect();
        match defs.fitting(k, &types) {
            Some(all) => {
                for (t, a) in sol.iter_mut().zip(all) {
                    if a != Ty::Unknown {
                        t.get_or_insert(a);
                    }
                }
                false
            }
            None => {
                self.errors
                    .push(self.no_implementation(body, call, k, &types));
                true
            }
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
        if let Some(field) = def.fields.iter().find(|f| hidden(def, f, call.pos)) {
            self.errors.push(
                Error::new(
                    call.pos,
                    format!(
                        "`{}` has the private field `{}`, so only the module that defines it builds one",
                        written_name(&def.name),
                        field.name
                    ),
                )
                .with_fix(format!(
                    "call a `pub` function of that module that gives a `{}`",
                    written_name(&def.name)
                )),
            );
        }
        let before = self.errors.len();
        let mut unknown = false;
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
                Some(i) => match defs.fields[s][i].partial(&sol) {
                    // A type that is not known yet is not one a mistake
                    // already explains.
                    Ty::Unknown if defs.fields[s][i] != Ty::Unknown => Want::Any,
                    t => Want::Ty(t),
                },
                None => Want::Ty(Ty::Unknown),
            };
            let gives = self.expr(body, &arg.value, &wanted);
            unknown |= gives.unknown();
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
                        self.settled = true;
                        self.fit(body, &arg.value, &gives, pattern, &mut sol, vars);
                        self.settled = false;
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
            && !unknown
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
        via: Option<(usize, &Want)>,
        placed: Option<&Ty>,
    ) -> Vec<Option<Ty>> {
        let vars = &signature.scope;
        // What the place of the call's result says the type parameters are,
        // which only a literal that nothing else decides takes.
        let mut from_place: Vec<Option<Ty>> = vec![None; vars.names.len()];
        if let Some(t) = placed {
            seed(&signature.gives, t, &mut from_place);
        }
        let mut sol: Vec<Option<Ty>> = vec![None; vars.names.len()];
        let comparing = call.args.len() == 2
            && matches!(&call.qualifier, Some((q, _)) if q == "Ord" || q == "Eq")
            && matches!(call.callee.as_str(), "compare" | "eq");
        for effect in &signature.effects {
            self.needs(body, effect, call.pos, &format!("`{}`", call.callee));
        }
        let mut given: Vec<bool> = vec![false; signature.params.len()];
        // The whole number literals whose type is not known when they are
        // reached, which are checked after the other arguments, with the
        // place of the parameter each is for.
        let mut deferred: Vec<(usize, &Expr)> = Vec::new();
        let errors_before = self.errors.len();
        let mut lambdas: Vec<(usize, &Lambda)> = Vec::new();
        // An argument that needs the type the others say is checked after them,
        // so a call that waits would be checked after it.
        let needs_others = (call.args.iter()).any(|a| {
            matches!(
                a.value,
                Expr::Lambda(_) | Expr::Case { .. } | Expr::List { .. }
            )
        });
        let mut named = false;
        // Whether an argument named a parameter there is not. The mistake is
        // one, so a parameter written as a pattern, which no name can reach,
        // is not also reported missing.
        let mut unknown = false;
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
                            "a lambda after a call is for its last parameter, and {}, the last \
                             parameter of `{callee}`, takes no function",
                            param_words(&signature.params[i].0, i)
                        ))
                    }
                    Some(i) if given[i] => Some(format!(
                        "the {} argument is given twice",
                        param_words(&signature.params[i].0, i)
                    )),
                    Some(i) => {
                        given[i] = true;
                        param = Some(i);
                        None
                    }
                };
                if let Some(message) = error {
                    self.errors.push(Error::new(arg.value.pos(), message));
                    self.refusals += 1;
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
                            None => {
                                unknown = true;
                                self.errors.push(Error::new(
                                    *pos,
                                    format!("`{}` has no parameter named `{name}`", call.callee),
                                ))
                            }
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
            if let (Some(via), Some(_), Expr::Lambda(_)) = (via, param, &arg.value)
                && !self.no_impl
            {
                self.no_impl = self.completed(body, call, via, signature, &mut sol);
            }
            // A lambda's parameters take the default of a type that nothing
            // before it settled.
            if let (Some(i), Expr::Lambda(_)) = (param, &arg.value) {
                for (p, d) in signature.param_defaults.iter().enumerate() {
                    if let (None, Some(d), true) = (&sol[p], d, signature.params[i].1.mentions(p)) {
                        let ty = d.solved(&sol);
                        sol[p] = (!ty.unknown()).then_some(ty);
                    }
                }
            }
            let quiet = self.no_impl || matches!(via, Some((_, Want::Ty(Ty::Unknown))));
            let wanted = match param {
                Some(_) if quiet => Want::Ty(Ty::Unknown),
                Some(i) => match signature.params[i].1.partial(&sol) {
                    // A type that is not known yet is not one a mistake
                    // already explains.
                    Ty::Unknown if signature.params[i].1 != Ty::Unknown => Want::Any,
                    t => Want::Ty(t),
                },
                None => Want::Ty(Ty::Unknown),
            };
            if let (Some(i), Want::Any) = (param, &wanted)
                && (matches!(arg.value, Expr::Number { .. })
                    || matches!(&arg.value, Expr::Match(m) if literal_blocks(m)))
            {
                deferred.push((i, &arg.value));
                continue;
            }
            // A call whose result a literal in it may decide, for a parameter
            // nothing says the type of yet, waits for the other arguments,
            // unless one of them needs what the earlier ones say.
            if let (Some(i), Expr::Call(_), Want::Any) = (param, &arg.value, &wanted)
                && !needs_others
            {
                deferred.push((i, &arg.value));
                continue;
            }
            // A list written out whose element type is not settled yet.
            if let (Some(i), Expr::List { .. }, Want::Ty(t)) = (param, &arg.value, &wanted)
                && *t != Ty::Unknown
                && t.unknown()
            {
                deferred.push((i, &arg.value));
                continue;
            }
            if let (Some(i), Expr::Lambda(l), Want::Ty(Ty::Fn(_))) = (param, &arg.value, &wanted) {
                lambdas.push((i, l));
            }
            let gives = self.expr(body, &arg.value, &wanted);
            match param {
                Some(i) => {
                    let pattern = &signature.params[i].1;
                    self.comparing = comparing;
                    self.fit(body, &arg.value, &gives, pattern, &mut sol, vars);
                    self.comparing = false;
                }
                None if gives == Ty::Nothing => {
                    self.errors.push(self.nothing_where(&arg.value, "text"));
                }
                None => {}
            }
        }
        // A literal takes the type the other arguments settle. When they do
        // not, only the first of them is reported.
        let mut reported = false;
        let (calls, numbers): (Vec<_>, Vec<_>) =
            (deferred.into_iter()).partition(|(_, value)| matches!(value, Expr::Call(_)));
        for (i, value) in calls.into_iter().chain(numbers) {
            let pattern = &signature.params[i].1;
            let wanted = match pattern.partial(&sol) {
                // A whole number that fits an `i64` is one when nothing
                // decides, with no mistake to report, so it is given its type.
                t if t.unknown() && !pattern.partial(&from_place).unknown() => {
                    Want::Ty(pattern.partial(&from_place))
                }
                t if t.unknown() && matches!(value, Expr::Call(_)) => Want::Any,
                t if t.unknown()
                    && (fits_i64(value)
                        || matches!(value, Expr::Match(m) if literal_blocks(m))) =>
                {
                    Want::Any
                }
                t if t.unknown() && reported => Want::Ty(Ty::Unknown),
                t if t.unknown() => {
                    reported = true;
                    Want::Any
                }
                t => Want::Ty(t),
            };
            let gives = self.expr(body, value, &wanted);
            self.comparing = comparing;
            self.fit(body, value, &gives, pattern, &mut sol, vars);
            self.comparing = false;
        }
        // The two sides of a comparison that are two ranges are taken as the
        // smallest range that holds both.
        if comparing
            && let [Ty::Int(a), Ty::Int(b)] = call
                .args
                .iter()
                .map(|a| {
                    self.types
                        .get(&addr(&a.value))
                        .cloned()
                        .unwrap_or(Ty::Unknown)
                })
                .collect::<Vec<_>>()[..]
            && a != b
            && let Some(h) = a.hull(b)
        {
            for (arg, int) in call.args.iter().zip([a, b]) {
                if int != h {
                    self.widened.insert(addr(&arg.value), h);
                }
            }
        }
        // A lambda whose parameters' types nothing decided, and which writes
        // none of them.
        if self.errors.len() == errors_before
            && let Some(l) = lambdas.iter().find_map(|&(i, l)| {
                let Ty::Fn(items) = signature.params[i].1.partial(&sol) else {
                    return None;
                };
                (l.types.iter().zip(&items))
                    .any(|(t, w)| t.is_none() && *w == Ty::Unknown)
                    .then_some(l)
            })
        {
            self.errors.push(
                Error::new(
                    l.pos,
                    "nothing decides the types of this lambda's parameters, so it writes them",
                )
                .with_fix("write each type, as in `fn(v: T) { ... }`"),
            );
        }
        let missing: Vec<String> = (signature.params.iter().enumerate())
            .zip(given.iter().zip(&signature.defaults))
            .filter(|(_, (given, default))| !**given && !**default)
            .filter(|((_, (p, _)), _)| !(unknown && p.contains('@')))
            .map(|((i, (p, _)), _)| param_words(p, i))
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
                    sentence(missing)
                ),
            ));
        }
        sol
    }

    /// Reports that what is at `pos`, which an error names as `what`, needs
    /// an effect the body does not list, unless that is already reported or
    /// a listed effect with no such name may be it misspelt. A default lists
    /// none, so each thing in it that needs one is reported.
    fn needs(&mut self, body: &mut Body, effect: &str, pos: Pos, what: &str) {
        if body.listed.contains(&effect) || body.unknown_effect {
            return;
        }
        if body.default.is_some() {
            self.errors.push(Error::new(
                pos,
                format!("a default carries no effect, and {what} needs the `{effect}` effect"),
            ));
            return;
        }
        if body.missing.iter().any(|m| m == effect) {
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
    fn recursion(&mut self, f: usize, path: &mut Set<Item>, done: &mut Set<Item>) {
        path.insert(Item::Fn(f));
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
            if path.contains(&Item::Fn(callee)) {
                self.errors.push(Error::unsupported(
                    pos,
                    "a function that calls itself, directly or through others,",
                ));
            } else if !done.contains(&Item::Fn(callee)) {
                self.recursion(callee, path, done);
            }
        }
        path.remove(&Item::Fn(f));
        done.insert(Item::Fn(f));
    }
}
