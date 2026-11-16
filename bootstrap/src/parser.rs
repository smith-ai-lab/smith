//! Builds the syntax tree of a file from its tokens, following the grammar
//! in `knowledge/grammar.md`. It accepts the part of the grammar the
//! compiler builds so far, and reports anything else the grammar allows as
//! not supported yet.

use crate::diag::{Error, Pos};
use crate::lexer::{Piece, Tok, Token};

/// The definitions of a file, each kind in the order they are written.
#[derive(Debug, PartialEq, Eq)]
pub struct File {
    pub functions: Vec<Function>,
    pub structs: Vec<Struct>,
    pub enums: Vec<Enum>,
    pub traits: Vec<Trait>,
    pub impls: Vec<Impl>,
}

/// A type parameter, as in `[T: Named]` or `[A, out B]`.
#[derive(Debug, PartialEq, Eq)]
pub struct Generic {
    pub name: String,
    pub pos: Pos,
    /// Where `out` is written, if it is.
    pub out: Option<Pos>,
    /// The trait after `:`, if there is one.
    pub bound: Option<Type>,
}

/// A struct definition.
#[derive(Debug, PartialEq, Eq)]
pub struct Struct {
    pub name: String,
    pub pos: Pos,
    pub generics: Vec<Generic>,
    pub fields: Vec<Field>,
}

/// An enum definition.
#[derive(Debug, PartialEq, Eq)]
pub struct Enum {
    pub name: String,
    pub pos: Pos,
    pub generics: Vec<Generic>,
    pub cases: Vec<CaseDef>,
}

/// A case of an enum, and the type it holds, if it holds one.
#[derive(Debug, PartialEq, Eq)]
pub struct CaseDef {
    pub name: String,
    pub pos: Pos,
    pub ty: Option<Type>,
}

/// A trait: a name, its types, and the signatures of its functions, each a
/// function with no body.
#[derive(Debug, PartialEq, Eq)]
pub struct Trait {
    pub name: String,
    pub pos: Pos,
    pub generics: Vec<Generic>,
    pub functions: Vec<Function>,
}

/// An implementation of a trait for the types in its brackets.
#[derive(Debug, PartialEq, Eq)]
pub struct Impl {
    /// Its own type parameters, written after `impl`.
    pub generics: Vec<Generic>,
    /// The trait, and where its name is.
    pub name: String,
    pub pos: Pos,
    pub args: Vec<Type>,
    pub functions: Vec<Function>,
}

/// A field of a struct, its type, and its default, if it has one.
#[derive(Debug, PartialEq, Eq)]
pub struct Field {
    pub name: String,
    pub pos: Pos,
    pub ty: Type,
    pub default: Option<Expr>,
}

/// A function definition.
#[derive(Debug, PartialEq, Eq)]
pub struct Function {
    pub name: String,
    pub pos: Pos,
    pub generics: Vec<Generic>,
    pub params: Vec<Param>,
    /// The type after `->`, if there is one.
    pub result: Option<Type>,
    pub effects: Effects,
    pub body: Vec<Stmt>,
    /// Where the `}` that closes the body is.
    pub end: Pos,
}

/// The effects after `!`, each with where it is written.
type Effects = Vec<(String, Pos)>;

/// A function's name and where it is, its type parameters, its
/// parameters, its result, and its effects.
type Signature = (String, Pos, Vec<Generic>, Vec<Param>, Option<Type>, Effects);

/// A parameter, its type, and its default, if it has one.
#[derive(Debug, PartialEq, Eq)]
pub struct Param {
    pub name: String,
    pub pos: Pos,
    pub ty: Type,
    pub default: Option<Expr>,
}

/// A type: a name, and the types in brackets after it, if any. A tuple
/// type has no name, and its elements are its `args`. A function type has
/// no name either, and its `args` are the types of its parameters followed
/// by the type it gives.
#[derive(Debug, PartialEq, Eq)]
pub struct Type {
    pub name: String,
    pub pos: Pos,
    pub args: Vec<Type>,
    /// Whether it is a function type, written `(T) -> U`.
    pub function: bool,
}

/// A statement of a block.
#[derive(Debug, PartialEq, Eq)]
pub enum Stmt {
    /// `x = e` or `x: T = e`, which defines a name, or `var x = e` or
    /// `var x: T = e`, which defines a name that can change. Whether a
    /// bare `x = e` defines `x` or assigns to a `var` is for the checker to
    /// decide.
    Define {
        name: String,
        pos: Pos,
        ty: Option<Type>,
        var: bool,
        value: Expr,
    },
    /// `x.f = e`, which assigns to a field of `x`, or to a field of a field
    /// for each further name.
    Assign {
        name: String,
        pos: Pos,
        fields: Vec<Named>,
        value: Expr,
    },
    /// `(a, var b) = e`, which takes a tuple apart. Whether each bare name
    /// in it defines a name or assigns to a `var` is for the checker to
    /// decide.
    Destructure {
        pattern: Pattern,
        ty: Option<Type>,
        value: Expr,
    },
    Expr(Expr),
}

/// An expression.
#[derive(Debug, PartialEq, Eq)]
pub enum Expr {
    /// A string literal with its escapes applied.
    Text {
        text: Vec<u8>,
        pos: Pos,
    },
    /// A string literal that holds expressions, as its parts in order.
    Interp {
        pos: Pos,
        parts: Vec<Part>,
    },
    Name {
        name: String,
        pos: Pos,
    },
    /// A call of a named function or of a name that holds a function, or
    /// the construction of a struct.
    Call(Call),
    /// `e.name`, which reads a field, or names a case in full when `e` is
    /// the name of an enum.
    Field {
        of: Box<Expr>,
        name: String,
        /// Where the field's name is.
        pos: Pos,
    },
    /// A case written with a leading dot, as in `.none` or `.cat(c)`, with
    /// the value it holds, if one is written.
    Case {
        name: String,
        /// Where the dot is.
        pos: Pos,
        payload: Option<Box<Expr>>,
    },
    Match(Box<Match>),
    /// A tuple, `()` or one of two values or more.
    Tuple {
        /// Where the `(` is.
        pos: Pos,
        items: Vec<Expr>,
    },
    Lambda(Box<Lambda>),
}

/// A part of a string literal that holds expressions.
#[derive(Debug, PartialEq, Eq)]
pub enum Part {
    /// Text, with its escapes applied, which is never empty.
    Text(Vec<u8>),
    /// An expression in braces, held as the call `Fmt.fmt(e)` that turns it
    /// into text. The call is where the expression is.
    Expr(Expr),
}

/// A lambda: a block, with `fn` and the names of its parameters before it
/// when it takes any.
#[derive(Debug, PartialEq, Eq)]
pub struct Lambda {
    /// Where it starts: at `fn`, or at the `{` when it takes no parameters.
    pub pos: Pos,
    pub params: Vec<Named>,
    pub body: Vec<Stmt>,
    /// Where the `}` that closes its body is.
    pub end: Pos,
}

/// `match e { pattern { ... } ... }`.
#[derive(Debug, PartialEq, Eq)]
pub struct Match {
    /// Where `match` is.
    pub pos: Pos,
    pub of: Expr,
    pub arms: Vec<Arm>,
}

/// A pattern of a `match` and the block that runs when it matches.
#[derive(Debug, PartialEq, Eq)]
pub struct Arm {
    pub pattern: Pattern,
    pub body: Vec<Stmt>,
    /// Where the `}` that closes the block is.
    pub end: Pos,
}

/// A pattern: a bare name, which matches anything and names it, a case
/// with a leading dot and the pattern for what it holds, if it holds one,
/// or a tuple of patterns.
#[derive(Debug, PartialEq, Eq)]
pub enum Pattern {
    Bind {
        name: String,
        /// Where the name is.
        pos: Pos,
        /// Where `var` is, if the name is marked as one that can change.
        var: Option<Pos>,
    },
    Case {
        name: String,
        /// Where the dot is.
        pos: Pos,
        inner: Option<Box<Pattern>>,
    },
    Tuple {
        /// Where the `(` is.
        pos: Pos,
        items: Vec<Pattern>,
    },
}

impl Pattern {
    /// Where the pattern starts.
    pub fn pos(&self) -> Pos {
        match self {
            Pattern::Bind { pos, var, .. } => var.unwrap_or(*pos),
            Pattern::Case { pos, .. } | Pattern::Tuple { pos, .. } => *pos,
        }
    }
}

impl Expr {
    /// Where the expression starts.
    pub fn pos(&self) -> Pos {
        match self {
            Expr::Text { pos, .. }
            | Expr::Interp { pos, .. }
            | Expr::Name { pos, .. }
            | Expr::Case { pos, .. }
            | Expr::Tuple { pos, .. } => *pos,
            Expr::Call(call) => call.pos,
            Expr::Field { of, .. } => of.pos(),
            Expr::Match(m) => m.pos,
            Expr::Lambda(l) => l.pos,
        }
    }
}

/// A call of a named function, or the construction of a struct, which is
/// written as a call. A function of a trait may be named with its trait,
/// as in `Named.name_of(x)`.
#[derive(Debug, PartialEq, Eq)]
pub struct Call {
    /// The name before the `.`, and where it is, if one is written.
    pub qualifier: Option<(String, Pos)>,
    pub callee: String,
    /// Where the call starts, which is where its qualifier is if it has
    /// one.
    pub pos: Pos,
    /// Where the callee's own name is.
    pub name_pos: Pos,
    pub args: Vec<Arg>,
    /// Where the closing parenthesis is, or where the lambda after the call
    /// starts when it has no parentheses.
    pub end: Pos,
}

/// An argument, given by position or by the name of its parameter, or a
/// lambda written after the call, which is its last argument and is for its
/// last parameter.
#[derive(Debug, PartialEq, Eq)]
pub struct Arg {
    pub name: Option<(String, Pos)>,
    pub value: Expr,
    /// Whether it is a lambda written after the call.
    pub after: bool,
}

/// Parses a file into its definitions.
///
/// Every definition and every statement is parsed on its own, so a mistake
/// in one is reported and does not hide a mistake in another. Within one
/// statement, or one function's signature, only the first mistake is
/// reported, as what follows it cannot be read with any confidence.
pub fn parse(tokens: &[Token]) -> Result<File, Vec<Error>> {
    let mut p = Parser {
        tokens,
        at: 0,
        errors: Vec::new(),
        no_lambda: false,
    };
    let mut functions = Vec::new();
    let mut structs = Vec::new();
    let mut enums = Vec::new();
    let mut traits = Vec::new();
    let mut impls = Vec::new();
    loop {
        p.skip_newlines();
        let start = p.at;
        match p.tok() {
            Tok::Eof => break,
            Tok::Keyword("fn") => {
                if let Ok(f) = p.function(start, false) {
                    functions.extend(f);
                }
            }
            Tok::Keyword("struct") => match p.structure() {
                Ok(s) => structs.push(s),
                Err(e) => {
                    p.errors.push(e);
                    p.skip_item(start);
                }
            },
            Tok::Keyword("enum") => match p.enumeration() {
                Ok(e) => enums.push(e),
                Err(e) => {
                    p.errors.push(e);
                    p.skip_item(start);
                }
            },
            Tok::Keyword("trait") => match p.declare_trait() {
                Ok(t) => traits.push(t),
                Err(e) => {
                    p.errors.push(e);
                    p.skip_item(start);
                }
            },
            Tok::Keyword("impl") => match p.implementation() {
                Ok(i) => impls.extend(i),
                Err(e) => {
                    p.errors.push(e);
                    p.skip_item(start);
                }
            },
            _ => {
                let error = p.item();
                p.errors.push(error);
                p.skip_item(start);
            }
        }
    }
    if p.errors.is_empty() {
        Ok(File {
            functions,
            structs,
            enums,
            traits,
            impls,
        })
    } else {
        Err(p.errors)
    }
}

/// A name and where it is.
type Named = (String, Pos);

/// The name, its place, and the fields a place names, if the expression is
/// one: a field of a name, or of a field of one.
fn place(expr: &Expr) -> Option<(String, Pos, Vec<Named>)> {
    match expr {
        Expr::Field { of, name, pos } => match &**of {
            Expr::Name {
                name: root,
                pos: at,
            } => Some((root.clone(), *at, vec![(name.clone(), *pos)])),
            of => {
                let (root, at, mut fields) = place(of)?;
                fields.push((name.clone(), *pos));
                Some((root, at, fields))
            }
        },
        _ => None,
    }
}

/// The expression in braces in a string, from its tokens, which end with
/// the `}` that closes it, as the call of `Fmt.fmt` that turns it into
/// text.
fn interpolated(tokens: &[Token]) -> Result<Expr, Error> {
    let mut p = Parser {
        tokens,
        at: 0,
        errors: Vec::new(),
        no_lambda: false,
    };
    let value = p.expr()?;
    if p.at + 1 != tokens.len() {
        return Err(p.expected("`}` to close the expression in the string", p.tok()));
    }
    let pos = value.pos();
    Ok(Expr::Call(Call {
        qualifier: Some(("Fmt".to_string(), pos)),
        callee: "fmt".to_string(),
        pos,
        name_pos: pos,
        args: vec![Arg {
            name: None,
            value,
            after: false,
        }],
        end: pos,
    }))
}

/// Whether a token can start an expression.
fn starts_expression(tok: &Tok) -> bool {
    matches!(
        tok,
        Tok::Ident(_)
            | Tok::Number(_)
            | Tok::Str(_)
            | Tok::Interp(_)
            | Tok::Punct("(" | "[" | "{" | "." | "-")
            | Tok::Keyword("not" | "try" | "if" | "match" | "for" | "loop" | "fn")
    )
}

/// Whether a token can continue an expression that comes before it.
fn continues_expression(tok: &Tok) -> bool {
    matches!(
        tok,
        Tok::Punct(
            "(" | "["
                | "."
                | ".."
                | "+"
                | "-"
                | "*"
                | "/"
                | "@"
                | "^"
                | "=="
                | "!="
                | "<"
                | "<="
                | ">"
                | ">="
        ) | Tok::Keyword("and" | "or")
    )
}

struct Parser<'a> {
    tokens: &'a [Token],
    at: usize,
    errors: Vec<Error>,
    /// Whether a `{` after a call opens the body of a construct, as it does
    /// after the value of a `match`, rather than a lambda. Inside
    /// parentheses and braces it opens a lambda again.
    no_lambda: bool,
}

impl Parser<'_> {
    fn tok(&self) -> &Tok {
        &self.tokens[self.at].tok
    }

    fn pos(&self) -> Pos {
        self.tokens[self.at].pos
    }

    fn next_tok(&self) -> &Tok {
        let next = (self.at + 1).min(self.tokens.len() - 1);
        &self.tokens[next].tok
    }

    fn bump(&mut self) {
        if self.at + 1 < self.tokens.len() {
            self.at += 1;
        }
    }

    fn skip_newlines(&mut self) {
        while self.tok() == &Tok::Newline {
            self.bump();
        }
    }

    fn expected(&self, what: &str, found: &Tok) -> Error {
        Error::new(
            self.pos(),
            format!("expected {what}, found {}", found.describe()),
        )
    }

    fn expect_punct(&mut self, p: &'static str, what: &str) -> Result<(), Error> {
        if self.tok() == &Tok::Punct(p) {
            self.bump();
            Ok(())
        } else {
            Err(self.expected(what, self.tok()))
        }
    }

    fn ident(&mut self, what: &str) -> Result<(String, Pos), Error> {
        let pos = self.pos();
        match self.tok() {
            Tok::Ident(name) => {
                let name = name.clone();
                self.bump();
                Ok((name, pos))
            }
            tok => Err(self.expected(what, tok)),
        }
    }

    /// Skips the tokens of a definition that holds a mistake, from its
    /// first token to the next token outside any bracket that starts a line
    /// at its first column. A brace, `requires`, or `ensures` there belongs
    /// to the definition before it, as a line break before them does not
    /// end it.
    fn skip_item(&mut self, from: usize) {
        self.at = from;
        let mut depth = 0usize;
        loop {
            match self.tok() {
                Tok::Eof => return,
                Tok::Newline if depth == 0 => {
                    self.skip_newlines();
                    let continues = matches!(
                        self.tok(),
                        Tok::Punct("{" | "}") | Tok::Keyword("requires" | "ensures")
                    );
                    if self.pos().col == 1 && !continues {
                        return;
                    }
                    continue;
                }
                Tok::Punct("(" | "[" | "{") => depth += 1,
                Tok::Punct(")" | "]" | "}") => depth = depth.saturating_sub(1),
                _ => {}
            }
            self.bump();
        }
    }

    /// Skips the tokens of a statement that holds a mistake, up to the line
    /// break or `}` that ends it. Gives whether one was found before the end
    /// of the file.
    ///
    /// A `}` with no `{` open in the statement closes the block, even inside
    /// a bracket the statement left open, so that a missing `)` is not also
    /// reported as a missing `}`.
    fn skip_statement(&mut self, from: usize) -> bool {
        self.at = from;
        let mut open: Vec<&str> = Vec::new();
        loop {
            match self.tok() {
                Tok::Eof => return false,
                Tok::Newline if open.is_empty() => return true,
                Tok::Punct("}") if !open.contains(&"{") => return true,
                Tok::Punct(p @ ("(" | "[" | "{")) => open.push(p),
                Tok::Punct(p @ (")" | "]" | "}")) => {
                    let opener = match *p {
                        ")" => "(",
                        "]" => "[",
                        _ => "{",
                    };
                    if let Some(i) = open.iter().rposition(|o| *o == opener) {
                        open.truncate(i);
                    }
                }
                _ => {}
            }
            self.bump();
        }
    }

    /// The error for a definition other than a function.
    fn item(&self) -> Error {
        let pos = self.pos();
        let feature = match self.tok() {
            Tok::Keyword("pub") => "`pub`",
            Tok::Keyword("effect") => "declaring an effect",
            Tok::Keyword("derive") => "`derive`",
            Tok::Keyword("without") => "`without`",
            Tok::Keyword("foreign") => "a foreign function",
            Tok::Keyword("use") => "`use`",
            Tok::Ident(_) => "a definition other than a function",
            tok => return self.expected("a definition", tok),
        };
        Error::unsupported(pos, feature)
    }

    /// Parses a struct and the line break after it.
    fn structure(&mut self) -> Result<Struct, Error> {
        self.bump();
        let (name, pos) = self.ident("the struct's name")?;
        let generics = self.generics()?;
        self.expect_punct("{", "`{` to open the struct's fields")?;
        let mut fields = Vec::new();
        loop {
            self.skip_newlines();
            match self.tok() {
                Tok::Punct("}") => break,
                Tok::Keyword("pub") => {
                    return Err(Error::unsupported(self.pos(), "`pub`"));
                }
                _ => {}
            }
            let (name, pos) = self.ident("a field or `}`")?;
            self.expect_punct(":", "`:` and the field's type")?;
            let ty = self.ty()?;
            let mut default = None;
            if self.tok() == &Tok::Punct("=") {
                self.bump();
                default = Some(self.expr()?);
            }
            match self.tok() {
                Tok::Newline | Tok::Punct("}") => {}
                tok => return Err(self.expected("a line break or `}`", tok)),
            }
            fields.push(Field {
                name,
                pos,
                ty,
                default,
            });
        }
        self.bump();
        match self.tok() {
            Tok::Newline | Tok::Eof => Ok(Struct {
                name,
                pos,
                generics,
                fields,
            }),
            tok => Err(self.expected("a line break", tok)),
        }
    }

    /// Parses an enum and the line break after it.
    fn enumeration(&mut self) -> Result<Enum, Error> {
        self.bump();
        let (name, pos) = self.ident("the enum's name")?;
        let generics = self.generics()?;
        self.expect_punct("{", "`{` to open the enum's cases")?;
        let mut cases = Vec::new();
        loop {
            self.skip_newlines();
            if self.tok() == &Tok::Punct("}") {
                break;
            }
            let (name, pos) = self.ident("a case or `}`")?;
            let mut ty = None;
            if self.tok() == &Tok::Punct(":") {
                self.bump();
                ty = Some(self.ty()?);
            }
            match self.tok() {
                Tok::Newline | Tok::Punct("}") => {}
                tok => return Err(self.expected("a line break or `}`", tok)),
            }
            cases.push(CaseDef { name, pos, ty });
        }
        self.bump();
        match self.tok() {
            Tok::Newline | Tok::Eof => Ok(Enum {
                name,
                pos,
                generics,
                cases,
            }),
            tok => Err(self.expected("a line break", tok)),
        }
    }

    /// Parses the type parameters in brackets after a name, if there are
    /// any.
    fn generics(&mut self) -> Result<Vec<Generic>, Error> {
        let mut generics = Vec::new();
        if self.tok() != &Tok::Punct("[") {
            return Ok(generics);
        }
        self.bump();
        self.skip_newlines();
        loop {
            let out = if self.tok() == &Tok::Keyword("out") {
                let at = self.pos();
                self.bump();
                Some(at)
            } else {
                None
            };
            let (name, pos) = self.ident("a type parameter")?;
            let bound = if self.tok() == &Tok::Punct(":") {
                self.bump();
                Some(self.ty()?)
            } else {
                None
            };
            generics.push(Generic {
                name,
                pos,
                out,
                bound,
            });
            self.skip_newlines();
            match self.tok() {
                Tok::Punct(",") => {
                    self.bump();
                    self.skip_newlines();
                    if self.tok() == &Tok::Punct("]") {
                        break;
                    }
                }
                Tok::Punct("]") => break,
                tok => return Err(self.expected("`,` or `]`", tok)),
            }
        }
        self.bump();
        Ok(generics)
    }

    /// Parses a trait and the line break after it.
    fn declare_trait(&mut self) -> Result<Trait, Error> {
        self.bump();
        let (name, pos) = self.ident("the trait's name")?;
        if self.tok() != &Tok::Punct("[") {
            return Err(self.expected("`[` and the trait's types", self.tok()));
        }
        let generics = self.generics()?;
        self.expect_punct("{", "`{` to open the trait's functions")?;
        let mut functions = Vec::new();
        loop {
            self.skip_newlines();
            match self.tok() {
                Tok::Punct("}") => break,
                Tok::Keyword("fn") => {}
                tok => return Err(self.expected("a function's signature or `}`", tok)),
            }
            let (name, pos, generics, params, result, effects) = self.signature()?;
            match self.tok() {
                Tok::Newline | Tok::Punct("}") => {}
                Tok::Keyword("requires" | "ensures") => {
                    return Err(Error::unsupported(self.pos(), "a contract"));
                }
                tok => return Err(self.expected("a line break or `}`", tok)),
            }
            functions.push(Function {
                name,
                pos,
                generics,
                params,
                result,
                effects,
                body: Vec::new(),
                end: pos,
            });
        }
        self.bump();
        match self.tok() {
            Tok::Newline | Tok::Eof => Ok(Trait {
                name,
                pos,
                generics,
                functions,
            }),
            tok => Err(self.expected("a line break", tok)),
        }
    }

    /// Parses an implementation. A mistake in its first line or in the
    /// signature of one of its functions is the one error reported for it,
    /// and a mistake in a statement of a function is reported along with
    /// those in its other statements, which gives `None`.
    fn implementation(&mut self) -> Result<Option<Impl>, Error> {
        self.bump();
        let generics = self.generics()?;
        let (name, pos) = self.ident("the trait's name")?;
        if self.tok() != &Tok::Punct("[") {
            return Err(self.expected("`[` and the types it is implemented for", self.tok()));
        }
        self.bump();
        let args = self.types()?;
        self.expect_punct("{", "`{` to open the implementation's functions")?;
        let mut functions = Vec::new();
        let mut ok = true;
        loop {
            self.skip_newlines();
            let start = self.at;
            match self.tok() {
                Tok::Punct("}") => break,
                Tok::Keyword("fn") => {}
                tok => return Err(self.expected("a function or `}`", tok)),
            }
            match self.function(start, true)? {
                Some(f) => functions.push(f),
                None => ok = false,
            }
        }
        self.bump();
        match self.tok() {
            Tok::Newline | Tok::Eof => Ok(ok.then_some(Impl {
                generics,
                name,
                pos,
                args,
                functions,
            })),
            tok => Err(self.expected("a line break", tok)),
        }
    }

    /// Parses a function that starts at the token `from`. The errors of its
    /// statements are recorded, and a function with any gives `None`.
    ///
    /// A mistake in the signature of a function inside an implementation
    /// is given back, for the implementation to report. Elsewhere it is
    /// recorded, and the function is skipped.
    fn function(&mut self, from: usize, nested: bool) -> Result<Option<Function>, Error> {
        let (name, pos, generics, params, result, effects) = match self.body_signature() {
            Ok(signature) => signature,
            Err(e) if nested => return Err(e),
            Err(e) => {
                self.errors.push(e);
                self.skip_item(from);
                return Ok(None);
            }
        };
        let mut body = Vec::new();
        let mut ok = true;
        let end = loop {
            self.skip_newlines();
            let start = self.at;
            match self.tok() {
                Tok::Punct("}") => {
                    let end = self.pos();
                    self.bump();
                    break end;
                }
                Tok::Eof => {
                    let error = self.expected("`}`", &Tok::Eof);
                    if nested {
                        return Err(error);
                    }
                    self.errors.push(error);
                    return Ok(None);
                }
                _ => match self.statement_line() {
                    Ok(stmt) => body.push(stmt),
                    Err(e) => {
                        self.errors.push(e);
                        ok = false;
                        if !self.skip_statement(start) {
                            return Ok(None);
                        }
                    }
                },
            }
        };
        match self.tok() {
            Tok::Newline | Tok::Eof => {}
            Tok::Punct("}") if nested => {}
            tok => {
                let error = self.expected("a line break", tok);
                if nested {
                    return Err(error);
                }
                self.errors.push(error);
                let at = self.at;
                self.skip_item(at);
                return Ok(None);
            }
        }
        Ok(ok.then_some(Function {
            name,
            pos,
            generics,
            params,
            result,
            effects,
            body,
            end,
        }))
    }

    /// Parses a function's signature, through the `{` that opens its body.
    fn body_signature(&mut self) -> Result<Signature, Error> {
        let signature = self.signature()?;
        self.skip_newlines();
        match self.tok() {
            Tok::Punct("{") => self.bump(),
            Tok::Keyword("requires" | "ensures") => {
                return Err(Error::unsupported(self.pos(), "a contract"));
            }
            tok => return Err(self.expected("`{` to open the function's body", tok)),
        }
        Ok(signature)
    }

    /// Parses a function's signature, through its list of effects.
    fn signature(&mut self) -> Result<Signature, Error> {
        self.bump();
        let (name, pos) = self.ident("the function's name")?;
        let generics = self.generics()?;
        self.expect_punct("(", "`(`")?;
        let mut params = Vec::new();
        self.skip_newlines();
        loop {
            match self.tok() {
                Tok::Punct(")") => break,
                Tok::Ident(_) => params.push(self.param()?),
                tok => return Err(self.expected("a parameter or `)`", tok)),
            }
            self.skip_newlines();
            match self.tok() {
                Tok::Punct(",") => {
                    self.bump();
                    self.skip_newlines();
                }
                Tok::Punct(")") => break,
                tok => return Err(self.expected("`,` or `)`", tok)),
            }
        }
        self.bump();
        let mut result = None;
        if self.tok() == &Tok::Punct("->") {
            self.bump();
            if matches!(self.tok(), Tok::Ident(_)) && self.next_tok() == &Tok::Punct(":") {
                return Err(Error::unsupported(self.pos(), "a named result"));
            }
            result = Some(self.ty()?);
        }
        let mut effects = Vec::new();
        if self.tok() == &Tok::Punct("!") {
            self.bump();
            effects.push(self.ident("an effect")?);
            while self.tok() == &Tok::Punct(",") {
                self.bump();
                self.skip_newlines();
                effects.push(self.ident("an effect")?);
            }
        }
        if self.tok() == &Tok::Newline
            && matches!(
                self.tokens.get(self.at + 1).map(|t| &t.tok),
                Some(Tok::Keyword("requires" | "ensures"))
            )
        {
            self.bump();
        }
        Ok((name, pos, generics, params, result, effects))
    }

    /// Parses `name: type`, and `= default` after it if it has one.
    fn param(&mut self) -> Result<Param, Error> {
        let (name, pos) = self.ident("a parameter")?;
        self.expect_punct(":", "`:` and the parameter's type")?;
        if self.tok() == &Tok::Keyword("take") {
            return Err(Error::unsupported(self.pos(), "`take`"));
        }
        let ty = self.ty()?;
        let mut default = None;
        if self.tok() == &Tok::Punct("=") {
            self.bump();
            self.skip_newlines();
            default = Some(self.expr()?);
        }
        Ok(Param {
            name,
            pos,
            ty,
            default,
        })
    }

    /// Parses a type: a name, and the types in brackets after it, if any.
    fn ty(&mut self) -> Result<Type, Error> {
        let pos = self.pos();
        match self.tok() {
            Tok::Ident(name) => {
                let name = name.clone();
                self.bump();
                let mut args = Vec::new();
                if self.tok() == &Tok::Punct("[") {
                    self.bump();
                    args = self.types()?;
                }
                Ok(Type {
                    name,
                    pos,
                    args,
                    function: false,
                })
            }
            Tok::Punct("[") => Err(Error::unsupported(pos, "an array type")),
            Tok::Punct("(") => {
                let mut args = self.list(Self::ty)?;
                if self.tok() == &Tok::Punct("->") {
                    self.bump();
                    args.push(self.ty()?);
                    if self.tok() == &Tok::Punct("!") {
                        return Err(Error::unsupported(
                            self.pos(),
                            "an effect on a function type",
                        ));
                    }
                    return Ok(Type {
                        name: String::new(),
                        pos,
                        args,
                        function: true,
                    });
                }
                if args.len() == 1 {
                    return Err(Error::unsupported(pos, "one type in parentheses"));
                }
                Ok(Type {
                    name: String::new(),
                    pos,
                    args,
                    function: false,
                })
            }
            tok => Err(self.expected("a type", tok)),
        }
    }

    /// Parses the items of a tuple, separated by commas, from its `(`
    /// through its `)`. There may be none.
    fn list<T>(&mut self, item: fn(&mut Self) -> Result<T, Error>) -> Result<Vec<T>, Error> {
        let no_lambda = std::mem::replace(&mut self.no_lambda, false);
        let items = self.list_items(item);
        self.no_lambda = no_lambda;
        items
    }

    fn list_items<T>(&mut self, item: fn(&mut Self) -> Result<T, Error>) -> Result<Vec<T>, Error> {
        self.bump();
        self.skip_newlines();
        let mut items = Vec::new();
        if self.tok() == &Tok::Punct(")") {
            self.bump();
            return Ok(items);
        }
        loop {
            items.push(item(self)?);
            self.skip_newlines();
            match self.tok() {
                Tok::Punct(",") => {
                    self.bump();
                    self.skip_newlines();
                }
                Tok::Punct(")") => break,
                tok => return Err(self.expected("`,` or `)`", tok)),
            }
        }
        self.bump();
        Ok(items)
    }

    /// Parses types separated by commas, after a `[`, through the `]`.
    fn types(&mut self) -> Result<Vec<Type>, Error> {
        let mut types = Vec::new();
        self.skip_newlines();
        loop {
            types.push(self.ty()?);
            self.skip_newlines();
            match self.tok() {
                Tok::Punct(",") => {
                    self.bump();
                    self.skip_newlines();
                    if self.tok() == &Tok::Punct("]") {
                        break;
                    }
                }
                Tok::Punct("]") => break,
                tok => return Err(self.expected("`,` or `]`", tok)),
            }
        }
        self.bump();
        Ok(types)
    }

    /// Parses a statement and the line break or `}` after it.
    fn statement_line(&mut self) -> Result<Stmt, Error> {
        let stmt = self.statement()?;
        match self.tok() {
            Tok::Newline | Tok::Punct("}") => Ok(stmt),
            tok if continues_expression(tok) => Err(self.continuation()),
            tok => Err(self.expected("a line break or `}`", tok)),
        }
    }

    fn statement(&mut self) -> Result<Stmt, Error> {
        let pos = self.pos();
        let feature = match self.tok() {
            Tok::Ident(name) => match self.next_tok() {
                Tok::Punct("=" | ":") => {
                    let name = name.clone();
                    self.bump();
                    return self.definition(name, pos, false);
                }
                _ => {
                    let expr = self.expr()?;
                    if self.tok() == &Tok::Punct("=")
                        && let Some((name, pos, fields)) = place(&expr)
                    {
                        self.bump();
                        let value = self.expr()?;
                        return Ok(Stmt::Assign {
                            name,
                            pos,
                            fields,
                            value,
                        });
                    }
                    self.assignment_not_built()?;
                    return Ok(Stmt::Expr(expr));
                }
            },
            Tok::Keyword("var") => {
                self.bump();
                let (name, pos) = self.ident("the name of the `var`")?;
                return self.definition(name, pos, true);
            }
            Tok::Punct("(") => return self.tuple_statement(),
            Tok::Keyword("with") => "`with`",
            Tok::Keyword("break") => "`break`",
            tok if starts_expression(tok) => return Ok(Stmt::Expr(self.expr()?)),
            tok => return Err(self.expected("a statement", tok)),
        };
        Err(Error::unsupported(pos, feature))
    }

    /// Parses a statement that starts like a tuple: one that takes a value
    /// apart, when a pattern is followed by `:` or `=`, and otherwise an
    /// expression.
    fn tuple_statement(&mut self) -> Result<Stmt, Error> {
        let start = self.at;
        let mistake = match self.pattern() {
            Ok(pattern) if matches!(self.tok(), Tok::Punct(":" | "=")) => {
                return self.destructure(pattern);
            }
            Ok(_) => None,
            Err(e) => Some(e),
        };
        self.at = start;
        let expr = self.expr()?;
        // A value followed by `=` is meant as a pattern, so the mistake that
        // keeps it from being one is the one to report.
        if let Some(e) = mistake
            && self.tok() == &Tok::Punct("=")
        {
            return Err(e);
        }
        self.assignment_not_built()?;
        Ok(Stmt::Expr(expr))
    }

    /// Parses the rest of a statement that takes a value apart, from the
    /// `:` or `=` after its pattern.
    fn destructure(&mut self, pattern: Pattern) -> Result<Stmt, Error> {
        let mut ty = None;
        if self.tok() == &Tok::Punct(":") {
            self.bump();
            ty = Some(self.ty()?);
        }
        self.assignment_not_built()?;
        self.expect_punct("=", "`=`")?;
        let value = self.expr()?;
        Ok(Stmt::Destructure { pattern, ty, value })
    }

    /// Parses the rest of a definition, from the `:` or `=` after its name.
    fn definition(&mut self, name: String, pos: Pos, var: bool) -> Result<Stmt, Error> {
        let mut ty = None;
        if self.tok() == &Tok::Punct(":") {
            self.bump();
            ty = Some(self.ty()?);
        }
        self.assignment_not_built()?;
        self.expect_punct("=", "`=`")?;
        let value = self.expr()?;
        Ok(Stmt::Define {
            name,
            pos,
            ty,
            var,
            value,
        })
    }

    /// The error for an accumulating assignment, which the compiler does not
    /// build yet.
    fn assignment_not_built(&self) -> Result<(), Error> {
        match self.tok() {
            Tok::Punct(op @ ("+=" | "-=" | "*=" | "/=")) => {
                Err(Error::unsupported(self.pos(), &format!("`{op}`")))
            }
            _ => Ok(()),
        }
    }

    /// The error for a token that would continue an expression, which the
    /// compiler does not build yet.
    fn continuation(&self) -> Error {
        let pos = self.pos();
        match self.tok() {
            Tok::Punct("(") => Error::unsupported(pos, "calling what this gives"),
            Tok::Punct("[") => Error::unsupported(pos, "an index"),
            tok => Error::unsupported(pos, &format!("the operator {}", tok.describe())),
        }
    }

    /// Parses an expression: text, a name, or a call, followed by the
    /// fields it reads.
    fn expr(&mut self) -> Result<Expr, Error> {
        let pos = self.pos();
        let mut expr = match self.tok() {
            Tok::Str(text) => {
                let text = text.clone();
                self.bump();
                Expr::Text { text, pos }
            }
            Tok::Interp(pieces) => {
                let pieces = pieces.clone();
                self.bump();
                let parts = (pieces.iter())
                    .map(|piece| match piece {
                        Piece::Text(text) => Ok(Part::Text(text.clone())),
                        Piece::Expr(tokens) => interpolated(tokens).map(Part::Expr),
                    })
                    .collect::<Result<_, _>>()?;
                Expr::Interp { pos, parts }
            }
            Tok::Ident(name) => {
                let name = name.clone();
                self.bump();
                self.call_or_name(None, name, pos)?
            }
            Tok::Punct(".") => {
                self.bump();
                let (name, _) = self.ident("a case's name")?;
                let mut payload = None;
                if self.tok() == &Tok::Punct("(") {
                    // A case takes no lambda after it.
                    let no_lambda = std::mem::replace(&mut self.no_lambda, true);
                    let call = self.call_rest(None, name.clone(), pos);
                    self.no_lambda = no_lambda;
                    let call = call?;
                    let mut args = call.args.into_iter();
                    payload = args.next().map(|arg| match arg.name {
                        Some((_, at)) => Err(Error::new(
                            at,
                            "the value a case holds is given by position",
                        )),
                        None => Ok(Box::new(arg.value)),
                    });
                    if let Some(extra) = args.next() {
                        return Err(Error::new(
                            extra.value.pos(),
                            "a case holds one value, and this is one more",
                        ));
                    }
                }
                Expr::Case {
                    name,
                    pos,
                    payload: payload.transpose()?,
                }
            }
            Tok::Keyword("match") => Expr::Match(Box::new(self.match_expr()?)),
            Tok::Punct("(") => {
                let mut items = self.list(Self::expr)?;
                if items.len() == 1 {
                    items.pop().expect("one")
                } else {
                    Expr::Tuple { pos, items }
                }
            }
            Tok::Punct("{") | Tok::Keyword("fn") => Expr::Lambda(Box::new(self.lambda()?)),
            tok if starts_expression(tok) => {
                let feature = match tok {
                    Tok::Number(_) => "a number",
                    Tok::Punct("[") => "an array",
                    Tok::Punct("-") => "the operator `-`",
                    Tok::Keyword(word) => {
                        return Err(Error::unsupported(pos, &format!("`{word}`")));
                    }
                    _ => "this expression",
                };
                return Err(Error::unsupported(pos, feature));
            }
            tok => return Err(self.expected("an expression", tok)),
        };
        while self.tok() == &Tok::Punct(".") {
            self.bump();
            let (name, pos) = self.ident("a field's name")?;
            expr = match expr {
                Expr::Name {
                    name: qualifier,
                    pos: at,
                } => self.call_or_name(Some((qualifier, at)), name, pos)?,
                of => Expr::Field {
                    of: Box::new(of),
                    name,
                    pos,
                },
            };
        }
        if continues_expression(self.tok()) {
            return Err(self.continuation());
        }
        Ok(expr)
    }

    /// Parses a `match`, through the `}` that closes it.
    fn match_expr(&mut self) -> Result<Match, Error> {
        let pos = self.pos();
        self.bump();
        let no_lambda = std::mem::replace(&mut self.no_lambda, true);
        let of = self.expr();
        self.no_lambda = no_lambda;
        let of = of?;
        self.expect_punct("{", "`{` to open the blocks of the `match`")?;
        let mut arms = Vec::new();
        loop {
            self.skip_newlines();
            if self.tok() == &Tok::Punct("}") {
                break;
            }
            let pattern = self.pattern()?;
            self.expect_punct("{", "`{` to open the pattern's block")?;
            let no_lambda = std::mem::replace(&mut self.no_lambda, false);
            let block = self.block_rest();
            self.no_lambda = no_lambda;
            let (body, end) = block?;
            match self.tok() {
                Tok::Newline | Tok::Punct("}") => {}
                tok => return Err(self.expected("a line break or `}`", tok)),
            }
            arms.push(Arm { pattern, body, end });
        }
        self.bump();
        Ok(Match { pos, of, arms })
    }

    /// Parses a pattern.
    fn pattern(&mut self) -> Result<Pattern, Error> {
        let pos = self.pos();
        match self.tok() {
            Tok::Ident(name) => {
                let name = name.clone();
                self.bump();
                Ok(Pattern::Bind {
                    name,
                    pos,
                    var: None,
                })
            }
            Tok::Keyword("var") => {
                self.bump();
                let (name, at) = self.ident("the name of the `var`")?;
                Ok(Pattern::Bind {
                    name,
                    pos: at,
                    var: Some(pos),
                })
            }
            Tok::Punct(".") => {
                self.bump();
                let (name, _) = self.ident("a case's name")?;
                let mut inner = None;
                if self.tok() == &Tok::Punct("(") {
                    self.bump();
                    inner = Some(Box::new(self.pattern()?));
                    self.expect_punct(")", "`)`")?;
                }
                Ok(Pattern::Case { name, pos, inner })
            }
            Tok::Punct("(") => {
                let items = self.list(Self::pattern)?;
                match items.len() {
                    0 => Err(Error::new(pos, "expected a pattern, found `()`")),
                    1 => Err(Error::unsupported(pos, "one pattern in parentheses")),
                    _ => Ok(Pattern::Tuple { pos, items }),
                }
            }
            tok => Err(self.expected("a pattern", tok)),
        }
    }

    /// Parses what follows a name, which is at `pos` and after the name
    /// before the `.` if one is written: a call, if parentheses or a lambda
    /// follow it, and otherwise the name, or the field of the name before
    /// the `.`.
    fn call_or_name(
        &mut self,
        qualifier: Option<(String, Pos)>,
        name: String,
        pos: Pos,
    ) -> Result<Expr, Error> {
        if self.tok() == &Tok::Punct("(") {
            return Ok(Expr::Call(self.call_rest(qualifier, name, pos)?));
        }
        if let Some(lambda) = self.lambda_after()? {
            let end = lambda.value.pos();
            return Ok(Expr::Call(Call {
                pos: qualifier.as_ref().map_or(pos, |(_, at)| *at),
                qualifier,
                callee: name,
                name_pos: pos,
                args: vec![lambda],
                end,
            }));
        }
        Ok(match qualifier {
            None => Expr::Name { name, pos },
            Some((q, at)) => Expr::Field {
                of: Box::new(Expr::Name { name: q, pos: at }),
                name,
                pos,
            },
        })
    }

    /// Parses a lambda written after a call, if one is, as the call's last
    /// argument.
    fn lambda_after(&mut self) -> Result<Option<Arg>, Error> {
        if self.no_lambda || !matches!(self.tok(), Tok::Punct("{") | Tok::Keyword("fn")) {
            return Ok(None);
        }
        Ok(Some(Arg {
            name: None,
            value: Expr::Lambda(Box::new(self.lambda()?)),
            after: true,
        }))
    }

    /// Parses a lambda, from its `fn` or its `{` through the `}` that closes
    /// it.
    fn lambda(&mut self) -> Result<Lambda, Error> {
        let pos = self.pos();
        let mut params = Vec::new();
        if self.tok() == &Tok::Keyword("fn") {
            self.bump();
            self.expect_punct("(", "`(` and the lambda's parameters")?;
            self.skip_newlines();
            if self.tok() == &Tok::Punct(")") {
                return Err(Error::new(
                    pos,
                    "a lambda that takes no parameters is a block alone, with no `fn()`",
                )
                .with_fix("leave out `fn()`"));
            }
            loop {
                params.push(self.ident("a parameter's name")?);
                self.skip_newlines();
                match self.tok() {
                    Tok::Punct(",") => {
                        self.bump();
                        self.skip_newlines();
                    }
                    Tok::Punct(")") => break,
                    tok => return Err(self.expected("`,` or `)`", tok)),
                }
            }
            self.bump();
        }
        self.expect_punct("{", "`{` to open the lambda's body")?;
        let no_lambda = std::mem::replace(&mut self.no_lambda, false);
        let body = self.block_rest();
        self.no_lambda = no_lambda;
        let (body, end) = body?;
        Ok(Lambda {
            pos,
            params,
            body,
            end,
        })
    }

    /// Parses the statements of a block, after its `{`, through the `}` that
    /// closes it, and gives where that is.
    fn block_rest(&mut self) -> Result<(Vec<Stmt>, Pos), Error> {
        let mut body = Vec::new();
        loop {
            self.skip_newlines();
            if self.tok() == &Tok::Punct("}") {
                let end = self.pos();
                self.bump();
                return Ok((body, end));
            }
            body.push(self.statement_line()?);
        }
    }

    /// Parses the arguments of a call, from its `(`.
    fn call_rest(
        &mut self,
        qualifier: Option<(String, Pos)>,
        callee: String,
        name_pos: Pos,
    ) -> Result<Call, Error> {
        let pos = qualifier.as_ref().map_or(name_pos, |(_, at)| *at);
        self.bump();
        let mut args = Vec::new();
        self.skip_newlines();
        let no_lambda = std::mem::replace(&mut self.no_lambda, false);
        let inside = self.args(&mut args);
        self.no_lambda = no_lambda;
        inside?;
        let end = self.pos();
        self.bump();
        args.extend(self.lambda_after()?);
        Ok(Call {
            qualifier,
            callee,
            pos,
            name_pos,
            args,
            end,
        })
    }

    /// Parses the arguments inside the parentheses of a call, up to the
    /// `)`.
    fn args(&mut self, args: &mut Vec<Arg>) -> Result<(), Error> {
        loop {
            if self.tok() == &Tok::Punct(")") {
                break;
            }
            args.push(self.arg()?);
            self.skip_newlines();
            match self.tok() {
                Tok::Punct(",") => {
                    self.bump();
                    self.skip_newlines();
                }
                Tok::Punct(")") => break,
                tok => return Err(self.expected("`,` or `)`", tok)),
            }
        }
        Ok(())
    }

    fn arg(&mut self) -> Result<Arg, Error> {
        let mut name = None;
        if let Tok::Ident(n) = self.tok()
            && self.next_tok() == &Tok::Punct(":")
        {
            name = Some((n.clone(), self.pos()));
            self.bump();
            self.bump();
            self.skip_newlines();
        }
        let value = self.expr()?;
        Ok(Arg {
            name,
            value,
            after: false,
        })
    }
}
