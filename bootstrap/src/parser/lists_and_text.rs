//! Parses lists and text: a string literal that holds expressions, a list
//! written out, and an index.

use super::*;

/// A part of a string literal that holds expressions.
#[derive(Debug, PartialEq, Eq)]
pub enum Part {
    /// Text, with its escapes applied, which is never empty.
    Text(Vec<u8>),
    /// An expression in braces, held as the call `Fmt.fmt(e)` that turns it
    /// into text. The call is where the expression is.
    Expr(Expr),
}

/// The expression in braces in a string, from its tokens, which end with
/// the `}` that closes it, as the call of `Fmt.fmt` that turns it into
/// text.
fn interpolated(
    tokens: &[Token],
    modules: &[(String, String)],
    qualified: &mut Vec<Qualified>,
) -> Result<Expr, Error> {
    let mut p = Parser {
        tokens,
        at: 0,
        errors: Vec::new(),
        unbuilt: Vec::new(),
        no_lambda: false,
        binds: Vec::new(),
        holes: Vec::new(),
        modules: modules.to_vec(),
        qualified: Vec::new(),
    };
    let value = p.expr()?;
    if p.at + 1 != tokens.len() {
        return Err(p.expected("`}` to close the expression in the string", p.tok()));
    }
    qualified.append(&mut p.qualified);
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

impl Parser<'_> {
    /// A string literal that holds expressions, from its pieces, which the
    /// lexer has found, starting at `pos`.
    pub(super) fn interpolation(&mut self, pieces: &[Piece], pos: Pos) -> Result<Expr, Error> {
        let mut parts: Vec<Part> = Vec::new();
        for piece in pieces {
            match piece {
                Piece::Text(text) => parts.push(Part::Text(text.clone())),
                Piece::Expr(tokens) => {
                    let e = interpolated(tokens, &self.modules, &mut self.qualified)?;
                    parts.push(Part::Expr(e));
                }
            }
        }
        Ok(Expr::Interp { pos, parts })
    }

    /// A list written out, `[]` or `[a, b, c]`, starting at the `[` at `pos`.
    pub(super) fn list_literal(&mut self, pos: Pos) -> Result<Expr, Error> {
        let items = self.list_until("]", Self::expr)?;
        Ok(Expr::List { pos, items })
    }

    /// `of[index]`, or `of[a, b]` where the index is a tuple, from the `[`.
    pub(super) fn index(&mut self, of: Expr) -> Result<Expr, Error> {
        let at = self.pos();
        self.bump();
        if self.tok() == &Tok::Punct("..") {
            return self.piece(of, None, at);
        }
        let mut index = self.either()?;
        if self.tok() == &Tok::Punct("..") {
            return self.piece(of, Some(index), at);
        }
        if continues_expression(self.tok()) {
            return Err(self.continuation());
        }
        if self.tok() == &Tok::Punct(",") {
            let mut items = vec![index];
            while self.tok() == &Tok::Punct(",") {
                self.bump();
                items.push(self.expr()?);
            }
            index = Expr::Tuple { pos: at, items };
        }
        self.expect_punct("]", "`]`")?;
        Ok(Expr::Index {
            of: Box::new(of),
            index: Box::new(index),
            pos: at,
        })
    }

    /// The rest of `of[a..b]`, `of[..b]`, or `of[a..]`, from the `..`.
    fn piece(&mut self, of: Expr, from: Option<Expr>, at: Pos) -> Result<Expr, Error> {
        self.bump();
        let to = match self.tok() {
            Tok::Punct("]") => None,
            _ => Some(Box::new(self.expr()?)),
        };
        self.expect_punct("]", "`]`")?;
        Ok(Expr::Slice {
            of: Box::new(of),
            from: from.map(Box::new),
            to,
            pos: at,
        })
    }
}
