//! Parses numbers and operators: a number or a character written out, with a
//! `-` before it, and `+`, `-`, `*`, `/`, `%`, and the comparisons.

use super::*;

/// The operators of whole number arithmetic.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ArithOp {
    Add,
    Sub,
    Mul,
    Div,
    Rem,
}

impl ArithOp {
    /// The operator as it is written.
    pub fn symbol(self) -> &'static str {
        match self {
            ArithOp::Add => "+",
            ArithOp::Sub => "-",
            ArithOp::Mul => "*",
            ArithOp::Div => "/",
            ArithOp::Rem => "%",
        }
    }
}

/// The literal for the number written as `text`, which the lexer has found
/// well formed, with a `-` before it if `negative`, starting at `pos`.
pub(super) fn number(text: String, negative: bool, pos: Pos) -> Result<Expr, Error> {
    if let Some(inner) = text.strip_prefix('\'') {
        let inner = &inner[..inner.len() - 1];
        let c = match inner {
            "\\'" => '\'',
            "\\\\" => '\\',
            "\\n" => '\n',
            "\\t" => '\t',
            _ => inner.chars().next().expect("a character the lexer found"),
        };
        return Ok(Expr::Number {
            magnitude: c as u128,
            text,
            negative,
            pos,
        });
    }
    let (digits, base) = match (text.strip_prefix("0x"), text.strip_prefix("0b")) {
        (Some(rest), _) => (rest, 16),
        (_, Some(rest)) => (rest, 2),
        _ if text.contains(['.', 'e', 'E']) => return Err(Error::unsupported(pos, "a float")),
        _ => (text.as_str(), 10),
    };
    let mut magnitude = 0u128;
    for c in digits.chars().filter(|c| *c != '_') {
        let digit = c.to_digit(base).expect("a digit the lexer found") as u128;
        magnitude = magnitude
            .checked_mul(base as u128)
            .and_then(|m| m.checked_add(digit))
            .unwrap_or(u128::MAX);
    }
    Ok(Expr::Number {
        text,
        magnitude,
        negative,
        pos,
    })
}

impl Parser<'_> {
    /// A number or a character written out as `text`, at `pos`, which is the
    /// current token.
    pub(super) fn number_literal(&mut self, text: String, pos: Pos) -> Result<Expr, Error> {
        self.bump();
        self.read_past(number(text, false, pos), pos)
    }

    /// The number, or a float, which is not built, in the place of which an
    /// `Expr::Unbuilt` is kept.
    fn read_past(&mut self, number: Result<Expr, Error>, pos: Pos) -> Result<Expr, Error> {
        match number {
            Err(e) if e.unbuilt => {
                self.unbuilt.push(e);
                Ok(Expr::Unbuilt { pos })
            }
            other => other,
        }
    }

    /// Whether the `-` at `pos`, the current token, is directly before a
    /// number.
    pub(super) fn negative_number_follows(&self, pos: Pos) -> bool {
        matches!(self.next_tok(), Tok::Number(_))
            && self.tokens[self.at + 1].pos
                == (Pos {
                    line: pos.line,
                    col: pos.col + 1,
                })
    }

    /// A number with a `-` directly before it, from the `-` at `pos`.
    pub(super) fn negative_number_literal(&mut self, pos: Pos) -> Result<Expr, Error> {
        self.bump();
        let Tok::Number(text) = self.tok().clone() else {
            unreachable!("a number")
        };
        self.bump();
        self.read_past(number(text, true, pos), pos)
    }

    /// Parses operands joined by `+` and `-`, which group to the left.
    pub(super) fn sum(&mut self) -> Result<Expr, Error> {
        let mut left = self.product()?;
        while let Tok::Punct(op @ ("+" | "-")) = self.tok() {
            let op = if *op == "+" {
                ArithOp::Add
            } else {
                ArithOp::Sub
            };
            left = self.arith(op, left, Self::product)?;
        }
        Ok(left)
    }

    /// Parses operands joined by `*`, `/`, and `%`, which group to the left.
    pub(super) fn product(&mut self) -> Result<Expr, Error> {
        let mut left = self.operand()?;
        while let Tok::Punct(op @ ("*" | "/" | "%")) = self.tok() {
            let op = match *op {
                "*" => ArithOp::Mul,
                "/" => ArithOp::Div,
                _ => ArithOp::Rem,
            };
            left = self.arith(op, left, Self::operand)?;
        }
        Ok(left)
    }

    /// Parses the right side of an arithmetic operator, from the operator,
    /// with `side`, and gives the operation.
    pub(super) fn arith(
        &mut self,
        op: ArithOp,
        left: Expr,
        side: fn(&mut Self) -> Result<Expr, Error>,
    ) -> Result<Expr, Error> {
        let op_pos = self.pos();
        self.bump();
        self.skip_newlines();
        let right = side(self)?;
        Ok(Expr::Arith {
            op,
            left: Box::new(left),
            right: Box::new(right),
            op_pos,
        })
    }

    /// Parses an operand, and a comparison of it with another if `==` or
    /// `!=` follows. A comparison takes no other comparison as an operand
    /// unless it is in parentheses.
    pub(super) fn comparison(&mut self) -> Result<Expr, Error> {
        let left = self.sum()?;
        let op = match self.tok() {
            Tok::Punct(op @ ("==" | "!=" | "<" | "<=" | ">" | ">=")) => *op,
            _ => return Ok(left),
        };
        let op_pos = self.pos();
        self.bump();
        self.skip_newlines();
        let right = if self.tok() == &Tok::Keyword("not") {
            self.negation()?
        } else {
            self.sum()?
        };
        if let Tok::Punct("==" | "!=" | "<" | "<=" | ">" | ">=") = self.tok() {
            return Err(Error::new(
                self.pos(),
                "a comparison cannot compare what another comparison gives \
                 unless one of them is in parentheses",
            )
            .with_fix("put the comparison that runs first in parentheses"));
        }
        Ok(compared(left, op, right, op_pos))
    }
}

/// The comparison of `left` with `right` by `op`, which is one of `==`,
/// `!=`, `<`, `<=`, `>`, and `>=`, written with the operator at `op_pos`.
pub(super) fn compared(left: Expr, op: &str, right: Expr, op_pos: Pos) -> Expr {
    {
        let pos = left.pos();
        let ordered = !matches!(op, "==" | "!=");
        let same = Expr::Call(Call {
            type_args: Vec::new(),
            qualifier: Some((if ordered { "Ord" } else { "Eq" }.to_string(), op_pos)),
            callee: if ordered { "compare" } else { "eq" }.to_string(),
            pos,
            name_pos: op_pos,
            args: [left, right]
                .into_iter()
                .map(|value| Arg {
                    name: None,
                    value,
                    after: false,
                })
                .collect(),
            end: op_pos,
        });
        match op {
            "==" => same,
            "!=" => negated(pos, same),
            // `a < b` is the `match` on how `a` and `b` stand that says
            // whether the answer is among the cases it holds.
            _ => {
                let holds = |case: &str| {
                    matches!(
                        (op, case),
                        ("<", "less")
                            | (">", "greater")
                            | ("<=", "less" | "equal")
                            | (">=", "greater" | "equal")
                    )
                };
                let arms = ["less", "equal", "greater"]
                    .into_iter()
                    .map(|case| {
                        let name = if holds(case) { "true" } else { "false" };
                        let value = Expr::Name {
                            name: name.to_string(),
                            pos: op_pos,
                        };
                        arm(case, op_pos, vec![Stmt::Expr(value)], op_pos)
                    })
                    .collect();
                Expr::Match(Box::new(Match {
                    pos,
                    of: same,
                    arms,
                    form: Form::Match,
                }))
            }
        }
    }
}
