//! The typed core term: what a program is once it has been checked, as
//! `knowledge/language/code.md` ("What code is") describes it. Everything the
//! language has is one of these nodes by then, and each holds its type and the
//! place in the source it came from.
//!
//! The nodes `for` and `loop` are not here: their shape is settled when the
//! checker gives them.

use crate::diag::Pos;

/// What a term gives. Every type here is settled: none is a type parameter or
/// a number whose range is not yet decided. `bool` is the prelude's enum with
/// the cases `false` and `true`, so it is an `Enum`; a float has no type here
/// until the checker produces one.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub enum Type {
    Text,
    /// A whole number of the range from `min` up to and including `max`.
    Int {
        min: i128,
        max: i128,
    },
    /// A struct, by its place among the file's structs, with a type for each
    /// of its type parameters.
    Struct(usize, Vec<Type>),
    /// An enum, by its place among the file's enums, with a type for each of
    /// its type parameters.
    Enum(usize, Vec<Type>),
    Tuple(Vec<Type>),
    List(Box<Type>),
    Slice(Box<Type>),
    /// A function: the types of its parameters, and the type it gives.
    Fn(Vec<Type>, Box<Type>),
    /// The empty tuple, which a function with no return type gives.
    Nothing,
}

impl Type {
    /// The whole number type of the range `min` to `max`.
    pub fn int(min: i128, max: i128) -> Type {
        Type::Int { min, max }
    }
}

/// A literal.
#[derive(Clone, Debug, PartialEq)]
pub enum Lit {
    Int(i128),
    Text(Vec<u8>),
}

/// What a `choose` matches the value against.
#[derive(Clone, Debug, PartialEq)]
pub enum Pattern {
    /// Anything, which binds no name.
    Any,
    /// Anything, bound to the name.
    Bind(String),
    Lit(Lit),
    /// A case of an enum, by its place among the enum's cases, with a
    /// pattern for what it holds.
    Case(usize, Option<Box<Pattern>>),
    Tuple(Vec<Pattern>),
}

/// What a `call` calls, which holds what it names.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Callee {
    /// A function of the file, by its place among them.
    Function(usize),
    /// A function of a trait's implementation, by the trait and the function.
    Trait(usize, usize),
    /// An operation of whole numbers, which no function of the file is.
    Operator(Operator),
}

/// The operations of whole numbers. A sum, a difference, and a product give
/// the exact range of the result; a comparison gives a `bool`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Operator {
    Add,
    Sub,
    Mul,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Equal,
}

impl Operator {
    /// How it is written.
    pub fn symbol(self) -> &'static str {
        match self {
            Operator::Add => "+",
            Operator::Sub => "-",
            Operator::Mul => "*",
            Operator::Less => "<",
            Operator::LessEqual => "<=",
            Operator::Greater => ">",
            Operator::GreaterEqual => ">=",
            Operator::Equal => "==",
        }
    }
}

/// A function of the file, checked: its parameters with their types, and its
/// body as a term.
#[derive(Clone, Debug, PartialEq)]
pub struct Function {
    pub name: String,
    /// The function's place among the file's functions, which a `call` names.
    pub place: usize,
    pub params: Vec<(String, Type)>,
    pub body: Term,
}

/// One node of the core term, with its type and place.
#[derive(Clone, Debug, PartialEq)]
pub struct Term {
    pub kind: Kind,
    pub ty: Type,
    pub pos: Pos,
}

/// The nodes.
#[derive(Clone, Debug, PartialEq)]
pub enum Kind {
    /// A literal.
    Lit(Lit),
    /// A parameter or a binding.
    Name(String),
    /// A call of a function that has a name, with its types settled: an
    /// operator, an index, `try`, and `convert` are each one.
    Call { callee: Callee, args: Vec<Term> },
    /// A call of a function held in a value.
    Apply {
        function: Box<Term>,
        args: Vec<Term>,
    },
    /// A function written where it is used.
    Lambda {
        params: Vec<(String, Type)>,
        body: Box<Term>,
    },
    /// A name given a value, and then what follows.
    Bind {
        name: String,
        value: Box<Term>,
        rest: Box<Term>,
    },
    /// A struct, a tuple, a case of an enum, or a list that is built. A case
    /// is the place of the enum and of the case among its cases.
    Make(Make, Vec<Term>),
    /// A part that is read.
    Field { of: Box<Term>, name: String },
    /// One block picked by a pattern: `if` and `match`.
    Choose {
        of: Box<Term>,
        arms: Vec<(Pattern, Term)>,
    },
}

/// What a `make` builds.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Make {
    /// A struct, by its place among the file's structs, with a value for each
    /// field in order.
    Struct(usize),
    Tuple,
    /// A case of an enum, by the enum and the case.
    Case(usize, usize),
    List,
}

impl Term {
    pub fn new(kind: Kind, ty: Type, pos: Pos) -> Term {
        Term { kind, ty, pos }
    }

    /// The terms directly inside this one, in the order they are written.
    pub fn parts(&self) -> Vec<&Term> {
        match &self.kind {
            Kind::Lit(_) | Kind::Name(_) => Vec::new(),
            Kind::Call { args, .. } | Kind::Make(_, args) => args.iter().collect(),
            Kind::Apply { function, args } => {
                std::iter::once(&**function).chain(args.iter()).collect()
            }
            Kind::Lambda { body, .. } => vec![body],
            Kind::Bind { value, rest, .. } => vec![value, rest],
            Kind::Field { of, .. } => vec![of],
            Kind::Choose { of, arms } => std::iter::once(&**of)
                .chain(arms.iter().map(|(_, b)| b))
                .collect(),
        }
    }

    /// How many nodes the term has, itself included.
    pub fn size(&self) -> usize {
        1 + self.parts().iter().map(|t| t.size()).sum::<usize>()
    }

    /// The term written out, one line, as `(node part part)`: a node with no
    /// part is written bare.
    pub fn show(&self) -> String {
        let (head, parts): (String, Vec<String>) = match &self.kind {
            Kind::Lit(Lit::Int(n)) => return n.to_string(),
            Kind::Lit(Lit::Text(t)) => return format!("{:?}", String::from_utf8_lossy(t)),
            Kind::Name(n) => return n.clone(),
            Kind::Call { callee, args } => (
                match callee {
                    Callee::Function(f) => format!("call f{f}"),
                    Callee::Trait(t, f) => format!("call t{t}.f{f}"),
                    Callee::Operator(o) => format!("call {}", o.symbol()),
                },
                args.iter().map(Term::show).collect(),
            ),
            Kind::Apply { function, args } => (
                "apply".to_string(),
                std::iter::once(function.show())
                    .chain(args.iter().map(Term::show))
                    .collect(),
            ),
            Kind::Lambda { params, body } => (
                format!(
                    "lambda {}",
                    params
                        .iter()
                        .map(|(n, _)| n.as_str())
                        .collect::<Vec<_>>()
                        .join(" ")
                ),
                vec![body.show()],
            ),
            Kind::Bind { name, value, rest } => {
                (format!("bind {name}"), vec![value.show(), rest.show()])
            }
            Kind::Make(make, args) => (
                match make {
                    Make::Struct(s) => format!("make s{s}"),
                    Make::Tuple => "make tuple".to_string(),
                    Make::Case(e, c) => format!("make e{e}.c{c}"),
                    Make::List => "make list".to_string(),
                },
                args.iter().map(Term::show).collect(),
            ),
            Kind::Field { of, name } => (format!("field {name}"), vec![of.show()]),
            Kind::Choose { of, arms } => (
                "choose".to_string(),
                std::iter::once(of.show())
                    .chain(
                        arms.iter()
                            .map(|(p, b)| format!("{} => {}", p.show(), b.show())),
                    )
                    .collect(),
            ),
        };
        if parts.is_empty() {
            head
        } else {
            format!("({head} {})", parts.join(" "))
        }
    }
}

impl Pattern {
    fn show(&self) -> String {
        match self {
            Pattern::Any => "_".to_string(),
            Pattern::Bind(n) => n.clone(),
            Pattern::Lit(Lit::Int(n)) => n.to_string(),
            Pattern::Lit(Lit::Text(t)) => format!("{:?}", String::from_utf8_lossy(t)),
            Pattern::Case(c, None) => format!(".c{c}"),
            Pattern::Case(c, Some(p)) => format!(".c{c}({})", p.show()),
            Pattern::Tuple(ps) => {
                format!(
                    "({})",
                    ps.iter().map(Pattern::show).collect::<Vec<_>>().join(", ")
                )
            }
        }
    }
}
