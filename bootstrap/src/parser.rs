//! Builds the syntax tree of a file from its tokens, following the grammar
//! in `knowledge/grammar.md`. It accepts the part of the grammar the
//! compiler builds so far, and reports anything else the grammar allows as
//! not supported yet.

use crate::diag::{Error, Pos};
use crate::lexer::{Tok, Token};

/// A function definition.
#[derive(Debug, PartialEq, Eq)]
pub struct Function {
    pub name: String,
    pub pos: Pos,
    /// The effects after `!`, each with where it is written.
    pub effects: Vec<(String, Pos)>,
    pub body: Call,
}

/// A call of a named function.
#[derive(Debug, PartialEq, Eq)]
pub struct Call {
    pub callee: String,
    pub pos: Pos,
    pub args: Vec<Arg>,
    /// Where the closing parenthesis is.
    pub end: Pos,
}

/// An argument given by position: a string literal with its escapes applied.
#[derive(Debug, PartialEq, Eq)]
pub struct Arg {
    pub text: Vec<u8>,
    pub pos: Pos,
}

/// Parses a file. A file with no definitions gives `None`.
pub fn parse(tokens: &[Token]) -> Result<Option<Function>, Error> {
    let mut p = Parser { tokens, at: 0 };
    p.skip_newlines();
    if p.tok() == &Tok::Eof {
        return Ok(None);
    }
    let function = p.item()?;
    p.skip_newlines();
    match p.tok() {
        Tok::Eof => Ok(Some(function)),
        tok if starts_item(tok) => Err(Error::unsupported(p.pos(), "a second definition")),
        tok => Err(p.expected("a definition", tok)),
    }
}

/// Whether a token can start a top-level item.
fn starts_item(tok: &Tok) -> bool {
    matches!(
        tok,
        Tok::Ident(_)
            | Tok::Keyword(
                "fn" | "pub"
                    | "struct"
                    | "enum"
                    | "trait"
                    | "effect"
                    | "impl"
                    | "derive"
                    | "without"
                    | "foreign"
                    | "use"
            )
    )
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

/// Whether a token can start a statement.
fn starts_statement(tok: &Tok) -> bool {
    starts_expression(tok) || matches!(tok, Tok::Keyword("var" | "with" | "break"))
}

struct Parser<'a> {
    tokens: &'a [Token],
    at: usize,
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

    fn item(&mut self) -> Result<Function, Error> {
        let pos = self.pos();
        let feature = match self.tok() {
            Tok::Keyword("fn") => return self.function(),
            Tok::Keyword("pub") => "`pub`",
            Tok::Keyword("struct") => "a struct",
            Tok::Keyword("enum") => "an enum",
            Tok::Keyword("trait") => "a trait",
            Tok::Keyword("effect") => "declaring an effect",
            Tok::Keyword("impl") => "`impl`",
            Tok::Keyword("derive") => "`derive`",
            Tok::Keyword("without") => "`without`",
            Tok::Keyword("foreign") => "a foreign function",
            Tok::Keyword("use") => "`use`",
            Tok::Ident(_) => "a definition other than a function",
            tok => return Err(self.expected("a definition", tok)),
        };
        Err(Error::unsupported(pos, feature))
    }

    fn function(&mut self) -> Result<Function, Error> {
        self.bump();
        let (name, pos) = self.ident("the function's name")?;
        if self.tok() == &Tok::Punct("[") {
            return Err(Error::unsupported(self.pos(), "a generic parameter"));
        }
        self.expect_punct("(", "`(`")?;
        self.skip_newlines();
        match self.tok() {
            Tok::Punct(")") => self.bump(),
            Tok::Ident(_) => return Err(Error::unsupported(self.pos(), "a parameter")),
            tok => return Err(self.expected("`)`", tok)),
        }
        if self.tok() == &Tok::Punct("->") {
            return Err(Error::unsupported(self.pos(), "a return type"));
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
        self.skip_newlines();
        if self.tok() == &Tok::Punct("}") {
            return Err(Error::unsupported(self.pos(), "a body with no statements"));
        }
        let body = self.statement()?;
        match self.tok() {
            Tok::Newline | Tok::Punct("}") => {}
            tok if continues_expression(tok) => {
                return Err(Error::unsupported(
                    self.pos(),
                    "an expression other than a call to `print`",
                ));
            }
            tok => return Err(self.expected("a line break or `}`", tok)),
        }
        self.skip_newlines();
        match self.tok() {
            Tok::Punct("}") => self.bump(),
            tok if starts_statement(tok) => {
                return Err(Error::unsupported(self.pos(), "a second statement"));
            }
            tok => return Err(self.expected("`}`", tok)),
        }
        match self.tok() {
            Tok::Newline | Tok::Eof => {}
            tok => return Err(self.expected("a line break", tok)),
        }
        Ok(Function {
            name,
            pos,
            effects,
            body,
        })
    }

    fn statement(&mut self) -> Result<Call, Error> {
        let pos = self.pos();
        let feature = match self.tok() {
            Tok::Ident(_) => match self.next_tok() {
                Tok::Punct("(") => return self.call(),
                Tok::Punct("=" | ":") => "a binding",
                Tok::Punct(":=" | "+=" | "-=" | "*=" | "/=") => "assignment",
                tok if continues_expression(tok)
                    || matches!(tok, Tok::Newline | Tok::Eof | Tok::Punct("}")) =>
                {
                    "an expression other than a call to `print`"
                }
                _ => {
                    let found = self.next_tok().clone();
                    self.bump();
                    return Err(self.expected("a line break or `}`", &found));
                }
            },
            Tok::Keyword("var") => "`var`",
            Tok::Keyword("with") => "`with`",
            Tok::Keyword("break") => "`break`",
            tok if starts_expression(tok) => "an expression other than a call to `print`",
            tok => return Err(self.expected("a statement", tok)),
        };
        Err(Error::unsupported(pos, feature))
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
                tok if continues_expression(tok) => {
                    return Err(Error::unsupported(
                        self.pos(),
                        "an argument other than a string literal",
                    ));
                }
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
        let pos = self.pos();
        match self.tok() {
            Tok::Str(text) => {
                let text = text.clone();
                self.bump();
                Ok(Arg { text, pos })
            }
            Tok::Ident(_) if self.next_tok() == &Tok::Punct(":") => {
                Err(Error::unsupported(pos, "a named argument"))
            }
            tok if starts_expression(tok) => Err(Error::unsupported(
                pos,
                "an argument other than a string literal",
            )),
            tok => Err(self.expected("an argument", tok)),
        }
    }
}
