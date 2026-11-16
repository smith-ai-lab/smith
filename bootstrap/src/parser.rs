//! Builds the syntax tree of a file from its tokens, following the grammar
//! in `knowledge/grammar.md`. It accepts the part of the grammar the
//! compiler builds so far, and reports anything else the grammar allows as
//! not supported yet.

use crate::diag::{Error, Pos};
use crate::lexer::{Tok, Token};

/// The definitions of a file, each kind in the order they are written.
#[derive(Debug, PartialEq, Eq)]
pub struct File {
    pub functions: Vec<Function>,
    pub structs: Vec<Struct>,
}

/// A struct definition.
#[derive(Debug, PartialEq, Eq)]
pub struct Struct {
    pub name: String,
    pub pos: Pos,
    pub fields: Vec<Field>,
}

/// A field of a struct, and its type.
#[derive(Debug, PartialEq, Eq)]
pub struct Field {
    pub name: String,
    pub pos: Pos,
    pub ty: Type,
}

/// A function definition.
#[derive(Debug, PartialEq, Eq)]
pub struct Function {
    pub name: String,
    pub pos: Pos,
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

/// A parameter and its type.
#[derive(Debug, PartialEq, Eq)]
pub struct Param {
    pub name: String,
    pub pos: Pos,
    pub ty: Type,
}

/// A type, written as a name.
#[derive(Debug, PartialEq, Eq)]
pub struct Type {
    pub name: String,
    pub pos: Pos,
}

/// A statement of a block.
#[derive(Debug, PartialEq, Eq)]
pub enum Stmt {
    /// `x = e`, or `x: T = e`, which defines a name.
    Define {
        name: String,
        pos: Pos,
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
    Name {
        name: String,
        pos: Pos,
    },
    /// A call of a named function, or the construction of a struct.
    Call(Call),
    /// `e.name`, which reads a field.
    Field {
        of: Box<Expr>,
        name: String,
        /// Where the field's name is.
        pos: Pos,
    },
}

impl Expr {
    /// Where the expression starts.
    pub fn pos(&self) -> Pos {
        match self {
            Expr::Text { pos, .. } | Expr::Name { pos, .. } => *pos,
            Expr::Call(call) => call.pos,
            Expr::Field { of, .. } => of.pos(),
        }
    }
}

/// A call of a named function, or the construction of a struct, which is
/// written as a call.
#[derive(Debug, PartialEq, Eq)]
pub struct Call {
    pub callee: String,
    pub pos: Pos,
    pub args: Vec<Arg>,
    /// Where the closing parenthesis is.
    pub end: Pos,
}

/// An argument, given by position or by the name of its parameter.
#[derive(Debug, PartialEq, Eq)]
pub struct Arg {
    pub name: Option<(String, Pos)>,
    pub value: Expr,
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
    };
    let mut functions = Vec::new();
    let mut structs = Vec::new();
    loop {
        p.skip_newlines();
        let start = p.at;
        match p.tok() {
            Tok::Eof => break,
            Tok::Keyword("fn") => functions.extend(p.function(start)),
            Tok::Keyword("struct") => match p.structure() {
                Ok(s) => structs.push(s),
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
        Ok(File { functions, structs })
    } else {
        Err(p.errors)
    }
}

/// Whether a token can start an expression.
fn starts_expression(tok: &Tok) -> bool {
    matches!(
        tok,
        Tok::Ident(_)
            | Tok::Number(_)
            | Tok::Str(_)
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
            Tok::Keyword("enum") => "an enum",
            Tok::Keyword("trait") => "a trait",
            Tok::Keyword("effect") => "declaring an effect",
            Tok::Keyword("impl") => "`impl`",
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
        if self.tok() == &Tok::Punct("[") {
            return Err(Error::unsupported(self.pos(), "a type parameter"));
        }
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
            match self.tok() {
                Tok::Punct("=") => {
                    return Err(Error::unsupported(self.pos(), "a default value"));
                }
                Tok::Newline | Tok::Punct("}") => {}
                tok => return Err(self.expected("a line break or `}`", tok)),
            }
            fields.push(Field { name, pos, ty });
        }
        self.bump();
        match self.tok() {
            Tok::Newline | Tok::Eof => Ok(Struct { name, pos, fields }),
            tok => Err(self.expected("a line break", tok)),
        }
    }

    /// Parses a function that starts at the token `from`. Its errors are
    /// recorded, and a function with any gives `None`.
    fn function(&mut self, from: usize) -> Option<Function> {
        let (name, pos, params, result, effects) = match self.signature() {
            Ok(signature) => signature,
            Err(e) => {
                self.errors.push(e);
                self.skip_item(from);
                return None;
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
                    self.errors.push(self.expected("`}`", &Tok::Eof));
                    return None;
                }
                _ => match self.statement_line() {
                    Ok(stmt) => body.push(stmt),
                    Err(e) => {
                        self.errors.push(e);
                        ok = false;
                        if !self.skip_statement(start) {
                            return None;
                        }
                    }
                },
            }
        };
        match self.tok() {
            Tok::Newline | Tok::Eof => {}
            tok => {
                self.errors.push(self.expected("a line break", tok));
                let at = self.at;
                self.skip_item(at);
                return None;
            }
        }
        ok.then_some(Function {
            name,
            pos,
            params,
            result,
            effects,
            body,
            end,
        })
    }

    /// Parses a function's signature, through the `{` that opens its body.
    #[allow(clippy::type_complexity)]
    fn signature(&mut self) -> Result<(String, Pos, Vec<Param>, Option<Type>, Effects), Error> {
        self.bump();
        let (name, pos) = self.ident("the function's name")?;
        if self.tok() == &Tok::Punct("[") {
            return Err(Error::unsupported(self.pos(), "a generic parameter"));
        }
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
        self.skip_newlines();
        match self.tok() {
            Tok::Punct("{") => self.bump(),
            Tok::Keyword("requires" | "ensures") => {
                return Err(Error::unsupported(self.pos(), "a contract"));
            }
            tok => return Err(self.expected("`{` to open the function's body", tok)),
        }
        Ok((name, pos, params, result, effects))
    }

    /// Parses `name: type`.
    fn param(&mut self) -> Result<Param, Error> {
        let (name, pos) = self.ident("a parameter")?;
        self.expect_punct(":", "`:` and the parameter's type")?;
        if self.tok() == &Tok::Keyword("take") {
            return Err(Error::unsupported(self.pos(), "`take`"));
        }
        let ty = self.ty()?;
        if self.tok() == &Tok::Punct("=") {
            return Err(Error::unsupported(self.pos(), "a default value"));
        }
        Ok(Param { name, pos, ty })
    }

    /// Parses a type, which so far is only ever a name.
    fn ty(&mut self) -> Result<Type, Error> {
        let pos = self.pos();
        match self.tok() {
            Tok::Ident(name) => {
                let name = name.clone();
                self.bump();
                if self.tok() == &Tok::Punct("[") {
                    return Err(Error::unsupported(self.pos(), "a type argument"));
                }
                Ok(Type { name, pos })
            }
            Tok::Punct("[") => Err(Error::unsupported(pos, "an array type")),
            Tok::Punct("(") => Err(Error::unsupported(pos, "a tuple or function type")),
            tok => Err(self.expected("a type", tok)),
        }
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
                    let mut ty = None;
                    if self.tok() == &Tok::Punct(":") {
                        self.bump();
                        ty = Some(self.ty()?);
                    }
                    self.expect_punct("=", "`=`")?;
                    let value = self.expr()?;
                    return Ok(Stmt::Define {
                        name,
                        pos,
                        ty,
                        value,
                    });
                }
                Tok::Punct(":=" | "+=" | "-=" | "*=" | "/=") => "assignment",
                _ => {
                    let expr = self.expr()?;
                    if matches!(expr, Expr::Field { .. })
                        && matches!(
                            self.tok(),
                            Tok::Punct("=" | ":=" | "+=" | "-=" | "*=" | "/=")
                        )
                    {
                        return Err(Error::unsupported(pos, "assigning to a field"));
                    }
                    return Ok(Stmt::Expr(expr));
                }
            },
            Tok::Keyword("var") => "`var`",
            Tok::Keyword("with") => "`with`",
            Tok::Keyword("break") => "`break`",
            tok if starts_expression(tok) => return Ok(Stmt::Expr(self.expr()?)),
            tok => return Err(self.expected("a statement", tok)),
        };
        Err(Error::unsupported(pos, feature))
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
            Tok::Ident(name) => {
                if self.next_tok() == &Tok::Punct("(") {
                    Expr::Call(self.call()?)
                } else {
                    let name = name.clone();
                    self.bump();
                    Expr::Name { name, pos }
                }
            }
            tok if starts_expression(tok) => {
                let feature = match tok {
                    Tok::Number(_) => "a number",
                    Tok::Punct("(") => "a tuple or an expression in parentheses",
                    Tok::Punct("[") => "an array",
                    Tok::Punct("{") => "a block",
                    Tok::Punct(".") => "a case",
                    Tok::Punct("-") => "the operator `-`",
                    Tok::Keyword("fn") => "a lambda",
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
            expr = Expr::Field {
                of: Box::new(expr),
                name,
                pos,
            };
        }
        if continues_expression(self.tok()) {
            return Err(self.continuation());
        }
        Ok(expr)
    }

    fn call(&mut self) -> Result<Call, Error> {
        let (callee, pos) = self.ident("a function's name")?;
        self.bump();
        let mut args = Vec::new();
        self.skip_newlines();
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
        let end = self.pos();
        self.bump();
        Ok(Call {
            callee,
            pos,
            args,
            end,
        })
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
        Ok(Arg { name, value })
    }
}
