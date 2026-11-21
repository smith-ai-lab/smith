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
    /// A step of a proof, which only the body of a proof has.
    Step(Step),
}

/// The steps a proof is made of, which the kernel knows and nothing else.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Step {
    /// `a == a`, and any statement with nothing unknown in it, which the
    /// kernel works out.
    Same,
    /// `unfold(f(x))`, which gives that `f(x)` equals the body of `f` with
    /// `x` put in.
    Unfold,
    /// `arith(p, q)`, which gives a fact about numbers that follows from the
    /// proofs it is handed.
    Arith,
}

impl Step {
    /// How it is written.
    pub fn name(self) -> &'static str {
        match self {
            Step::Same => "same",
            Step::Unfold => "unfold",
            Step::Arith => "arith",
        }
    }
}

/// A proof function: its parameters, the statement it proves as a term, that
/// statement as it is written, and the steps of its body as a term.
#[derive(Clone, Debug, PartialEq)]
pub struct Proof {
    pub name: String,
    pub pos: Pos,
    pub params: Vec<(String, Type)>,
    pub statement: Term,
    pub written: String,
    pub body: Term,
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
    /// `xs[i]`, the element of a list or a slice at a place.
    Index,
    /// `xs.size`, how many elements a list, a slice, or a text has, which a
    /// text counts in bytes.
    Size,
    /// `t.bytes`, the bytes of a text, as a slice.
    Bytes,
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
            Operator::Index => "[]",
            Operator::Size => "size",
            Operator::Bytes => "bytes",
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
    /// A part that is read: the struct's field, or the tuple's, by its name
    /// and its place among them.
    Field {
        of: Box<Term>,
        name: String,
        index: usize,
    },
    /// One block picked by a pattern: `if` and `match`.
    Choose {
        of: Box<Term>,
        arms: Vec<(Pattern, Term)>,
    },
    /// Rounds in order, each handing values to the next. Each round binds
    /// `counter` to the next of `source` and the names of `state` to what
    /// the last round handed on, which for the first round is what each
    /// held when the loop began, and works `body` out, which is a `Next` to
    /// go on or a `Done` to stop. The loop gives the tuple of the values it
    /// ended with, which is the unit when it has no state. A loop over a
    /// `source` that has no more ends with what the last round handed on.
    Loop {
        source: Source,
        counter: String,
        state: Vec<(String, Term)>,
        body: Box<Term>,
    },
    /// The end of a round of a `loop` that goes on, with the values of the
    /// state for the next, in the order of the loop's state.
    Next(Vec<Term>),
    /// The end of a round of a `loop` that stops, with the values of the
    /// state it ends with.
    Done(Vec<Term>),
}

/// What a `loop` takes a value from in each round.
#[derive(Clone, Debug, PartialEq)]
pub enum Source {
    /// The numbers from the first up to, and not including, the second.
    Range(Box<Term>, Box<Term>),
    /// The elements of a list, in order.
    Over(Box<Term>),
    /// Nothing: the rounds go on until a `Done`.
    Open,
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
            Kind::Loop {
                source,
                state,
                body,
                ..
            } => {
                let mut parts: Vec<&Term> = match source {
                    Source::Range(a, b) => vec![a, b],
                    Source::Over(of) => vec![of],
                    Source::Open => Vec::new(),
                };
                parts.extend(state.iter().map(|(_, t)| t));
                parts.push(body);
                parts
            }
            Kind::Next(values) | Kind::Done(values) => values.iter().collect(),
        }
    }

    /// Puts each term directly inside this one as `f` gives, in place.
    pub fn rebuild<E>(&mut self, f: &mut dyn FnMut(&Term) -> Result<Term, E>) -> Result<(), E> {
        match &mut self.kind {
            Kind::Lit(_) | Kind::Name(_) => {}
            Kind::Call { args, .. } | Kind::Make(_, args) => {
                for a in args {
                    *a = f(a)?;
                }
            }
            Kind::Apply { function, args } => {
                **function = f(function)?;
                for a in args {
                    *a = f(a)?;
                }
            }
            Kind::Lambda { body, .. } => **body = f(body)?,
            Kind::Bind { value, rest, .. } => {
                **value = f(value)?;
                **rest = f(rest)?;
            }
            Kind::Field { of, .. } => **of = f(of)?,
            Kind::Choose { of, arms } => {
                **of = f(of)?;
                for (_, b) in arms {
                    *b = f(b)?;
                }
            }
            Kind::Loop {
                source,
                state,
                body,
                ..
            } => {
                match source {
                    Source::Range(a, b) => {
                        **a = f(a)?;
                        **b = f(b)?;
                    }
                    Source::Over(of) => **of = f(of)?,
                    Source::Open => {}
                }
                for (_, t) in state {
                    *t = f(t)?;
                }
                **body = f(body)?;
            }
            Kind::Next(values) | Kind::Done(values) => {
                for v in values {
                    *v = f(v)?;
                }
            }
        }
        Ok(())
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
                    Callee::Step(s) => format!("call {}", s.name()),
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
            Kind::Field { of, name, .. } => (format!("field {name}"), vec![of.show()]),
            Kind::Loop {
                source,
                counter,
                state,
                body,
            } => {
                let mut parts = match source {
                    Source::Range(a, b) => vec![format!("{counter} in {}..{}", a.show(), b.show())],
                    Source::Over(of) => vec![format!("{counter} in {}", of.show())],
                    Source::Open => vec!["open".to_string()],
                };
                parts.extend(state.iter().map(|(n, t)| format!("{n}={}", t.show())));
                parts.push(body.show());
                ("loop".to_string(), parts)
            }
            Kind::Next(values) => ("next".to_string(), values.iter().map(Term::show).collect()),
            Kind::Done(values) => ("done".to_string(), values.iter().map(Term::show).collect()),
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
