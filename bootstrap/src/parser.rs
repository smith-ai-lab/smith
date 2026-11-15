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
    pub effects: Effects,
    pub body: Call,
}

/// The effects after `!`, each with where it is written.
type Effects = Vec<(String, Pos)>;

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
///
/// Every definition and every statement is parsed on its own, so a mistake
/// in one is reported and does not hide a mistake in another. Within one
/// statement, or one function's signature, only the first mistake is
/// reported, as what follows it cannot be read with any confidence.
pub fn parse(tokens: &[Token]) -> Result<Option<Function>, Vec<Error>> {
    let mut p = Parser {
        tokens,
        at: 0,
        errors: Vec::new(),
    };
    let mut function = None;
    let mut seen_function = false;
    loop {
        p.skip_newlines();
        let start = p.at;
        let pos = p.pos();
        match p.tok() {
            Tok::Eof => break,
            Tok::Keyword("fn") if seen_function => {
                p.errors
                    .push(Error::unsupported(pos, "a definition after the first"));
                p.skip_item(start);
            }
            Tok::Keyword("fn") => {
                seen_function = true;
                function = p.function(start);
            }
            _ => {
                let error = p.item();
                p.errors.push(error);
                p.skip_item(start);
            }
        }
    }
    if p.errors.is_empty() {
        Ok(function)
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
    /// at its first column. A brace there belongs to the definition before
    /// it, as when a body's `{` is on a line of its own.
    fn skip_item(&mut self, from: usize) {
        self.at = from;
        let mut depth = 0usize;
        loop {
            match self.tok() {
                Tok::Eof => return,
                Tok::Newline if depth == 0 => {
                    self.skip_newlines();
                    if self.pos().col == 1 && !matches!(self.tok(), Tok::Punct("{" | "}")) {
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
    fn skip_statement(&mut self, from: usize) -> bool {
        self.at = from;
        let mut depth = 0usize;
        loop {
            match self.tok() {
                Tok::Eof => return false,
                Tok::Newline | Tok::Punct("}") if depth == 0 => return true,
                Tok::Punct("(" | "[" | "{") => depth += 1,
                Tok::Punct(")" | "]" | "}") => depth = depth.saturating_sub(1),
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
            tok => return self.expected("a definition", tok),
        };
        Error::unsupported(pos, feature)
    }

    /// Parses a function that starts at the token `from`. Its errors are
    /// recorded, and a function with any gives `None`.
    fn function(&mut self, from: usize) -> Option<Function> {
        let (name, pos, effects) = match self.signature() {
            Ok(signature) => signature,
            Err(e) => {
                self.errors.push(e);
                self.skip_item(from);
                return None;
            }
        };
        let mut body = None;
        let mut ok = true;
        let mut count = 0;
        loop {
            self.skip_newlines();
            let start = self.at;
            let at = self.pos();
            match self.tok() {
                Tok::Punct("}") => {
                    if count == 0 {
                        self.errors
                            .push(Error::unsupported(at, "a body with no statements"));
                        ok = false;
                    }
                    self.bump();
                    break;
                }
                Tok::Eof => {
                    self.errors.push(self.expected("`}`", &Tok::Eof));
                    return None;
                }
                _ => {
                    match self.statement_line() {
                        Ok(call) if count == 0 => body = Some(call),
                        Ok(_) => {
                            self.errors
                                .push(Error::unsupported(at, "a statement after the first"));
                            ok = false;
                        }
                        Err(e) => {
                            self.errors.push(e);
                            ok = false;
                            if !self.skip_statement(start) {
                                return None;
                            }
                        }
                    }
                    count += 1;
                }
            }
        }
        match self.tok() {
            Tok::Newline | Tok::Eof => {}
            tok => {
                self.errors.push(self.expected("a line break", tok));
                let at = self.at;
                self.skip_item(at);
                return None;
            }
        }
        let body = body.filter(|_| ok)?;
        Some(Function {
            name,
            pos,
            effects,
            body,
        })
    }

    /// Parses a function's signature, through the `{` that opens its body.
    fn signature(&mut self) -> Result<(String, Pos, Effects), Error> {
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
        Ok((name, pos, effects))
    }

    /// Parses a statement and the line break or `}` after it.
    fn statement_line(&mut self) -> Result<Call, Error> {
        let call = self.statement()?;
        match self.tok() {
            Tok::Newline | Tok::Punct("}") => Ok(call),
            tok if continues_expression(tok) => Err(Error::unsupported(
                self.pos(),
                "an expression other than a call to `print`",
            )),
            tok => Err(self.expected("a line break or `}`", tok)),
        }
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
