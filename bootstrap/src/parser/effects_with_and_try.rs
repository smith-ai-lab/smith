//! Parses the feature of effects, `with`, and `try`: the effects after `!` in a
//! signature, `with`, which passes the rest of a block to a call, and `try`.

use super::*;

/// An effect: a name, and the operations it lists. Each operation is also one
/// of the file's functions, a function with no body that carries the effect,
/// so it is imported and called as any function is.
#[derive(Debug, PartialEq, Eq)]
pub struct EffectDef {
    pub name: String,
    pub pos: Pos,
    /// The names of its operations, in the order written.
    pub operations: Vec<String>,
    /// For each operation, where `pub` is written before it, if it is. An
    /// operation is private to its module unless it is.
    pub public: Vec<Option<Pos>>,
}

/// Whether a function is an operation of an effect, which has no body and
/// ends where it starts.
pub fn is_operation(f: &Function) -> bool {
    f.body.is_empty() && f.end == f.pos
}

/// `try operand`, with `try` at `pos`: what `operand` splits into goes on
/// with the value of `.go`, and leaves the function with that of `.stop`.
pub(super) fn split(pos: Pos, operand: Expr) -> Expr {
    let go = format!("try@{}:{}", pos.line, pos.col);
    let stop = format!("stop@{}:{}", pos.line, pos.col);
    let name = |name: &String| Expr::Name {
        name: name.clone(),
        pos,
    };
    let case = |case: &str, held: &String, body: Expr| Arm {
        pattern: Pattern::Case {
            name: case.to_string(),
            pos,
            inner: Some(Box::new(Pattern::Bind {
                name: held.clone(),
                pos,
                var: None,
            })),
        },
        body: vec![Stmt::Expr(body)],
        end: pos,
    };
    let leave = Expr::Return {
        value: Some(Box::new(name(&stop))),
        pos,
    };
    Expr::Match(Box::new(Match {
        pos,
        of: Expr::Call(Call {
            qualifier: Some(("Try".to_string(), pos)),
            callee: "split".to_string(),
            pos,
            name_pos: pos,
            args: vec![Arg {
                name: None,
                value: operand,
                after: false,
            }],
            end: pos,
        }),
        arms: vec![case("go", &go, name(&go)), case("stop", &stop, leave)],
        form: Form::Try,
    }))
}

/// A line of a block: a statement, or a `with`.
pub(super) enum Line {
    Stmt(Stmt),
    With(Wrapper),
}

/// What a statement passes the rest of its block to: a call, with a lambda
/// to be written after it that holds the rest of the block. The lambda
/// starts with the statements in `lead`, which bind what it is given.
pub(super) struct Wrapper {
    call: Call,
    pos: Pos,
    params: Vec<Named>,
    lead: Vec<Stmt>,
}

/// The statements of a block as they are read: those in the lambda the last
/// `with` or `try` opened, and, for each such lambda, the statements before
/// it and what it is passed to.
#[derive(Default)]
pub(super) struct Block {
    pub(super) outer: Vec<(Vec<Stmt>, Wrapper)>,
    pub(super) inner: Vec<Stmt>,
}

impl Block {
    /// The rest of the block is passed to the call.
    pub(super) fn wrap(&mut self, mut w: Wrapper) {
        let lead = std::mem::take(&mut w.lead);
        let before = std::mem::replace(&mut self.inner, lead);
        self.outer.push((before, w));
    }

    /// The statements of the block, each lambda closed by the `}` at `end`.
    pub(super) fn finish(mut self, end: Pos) -> Vec<Stmt> {
        while let Some((mut before, w)) = self.outer.pop() {
            let lambda = Expr::Lambda(Box::new(Lambda {
                pos: w.pos,
                types: w.params.iter().map(|_| None).collect(),
                params: w.params,
                result: None,
                body: std::mem::take(&mut self.inner),
                end,
            }));
            let mut call = w.call;
            call.args.push(Arg {
                name: None,
                value: lambda,
                after: true,
            });
            before.push(Stmt::Expr(Expr::Call(call)));
            self.inner = before;
        }
        self.inner
    }
}

impl Parser<'_> {
    /// Parses the effects after `!` in a function's signature, if it has any.
    pub(super) fn effects(&mut self) -> Result<Effects, Error> {
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
        Ok(effects)
    }

    /// Parses an effect and the line break after it: its operations are
    /// returned as functions, each carrying the effect.
    pub(super) fn declare_effect(&mut self) -> Result<(EffectDef, Vec<Function>), Error> {
        self.bump();
        let (name, pos) = self.defined("the effect's name")?;
        self.expect_punct("{", "`{` to open the effect's operations")?;
        let mut operations = Vec::new();
        let mut public = Vec::new();
        loop {
            self.skip_newlines();
            let mut marked = None;
            if self.tok() == &Tok::Keyword("pub") {
                marked = Some(self.pos());
                self.bump();
            }
            match self.tok() {
                Tok::Punct("}") if marked.is_none() => break,
                Tok::Keyword("fn") => {}
                tok => return Err(self.expected("an operation's signature or `}`", tok)),
            }
            public.push(marked);
            let (op, at, generics, params, result, result_name, mut effects) = self.signature()?;
            match self.tok() {
                Tok::Newline | Tok::Punct("}") => {}
                Tok::Keyword("requires" | "ensures") => {
                    return Err(Error::unsupported(self.pos(), "a contract"));
                }
                tok => return Err(self.expected("a line break or `}`", tok)),
            }
            effects.insert(0, (name.clone(), at));
            operations.push(Function {
                name: op,
                pos: at,
                generics,
                params,
                result,
                result_name,
                effects,
                requires: Vec::new(),
                needs: Vec::new(),
                ensures: Vec::new(),
                body: Vec::new(),
                end: at,
            });
        }
        self.bump();
        match self.tok() {
            Tok::Newline | Tok::Eof => {}
            tok => return Err(self.expected("a line break", tok)),
        }
        let names = operations.iter().map(|f| f.name.clone()).collect();
        Ok((
            EffectDef {
                name,
                pos,
                operations: names,
                public,
            },
            operations,
        ))
    }

    /// Parses `with e` or `with x = e`, whose call `e` is passed the rest
    /// of the block as a lambda that takes `x`.
    pub(super) fn with(&mut self) -> Result<Wrapper, Error> {
        let pos = self.pos();
        self.bump();
        let mut params = Vec::new();
        let mut lead = Vec::new();
        let binds = match self.tok() {
            Tok::Ident(_) => self.next_tok() == &Tok::Punct("="),
            Tok::Keyword("var") | Tok::Punct("(") => true,
            _ => false,
        };
        if binds {
            let pattern = self.pattern()?;
            self.expect_punct("=", "`=` after the pattern of `with`")?;
            match pattern {
                Pattern::Bind {
                    name,
                    pos,
                    var: None,
                } => params.push((name, pos)),
                pattern => {
                    let name = format!("with@{}:{}", pos.line, pos.col);
                    let value = Expr::Name {
                        name: name.clone(),
                        pos,
                    };
                    lead.push(match pattern {
                        Pattern::Bind {
                            name: x,
                            pos: at,
                            var: Some(_),
                        } => Stmt::Define {
                            name: x,
                            pos: at,
                            ty: None,
                            var: true,
                            value,
                        },
                        pattern => Stmt::Destructure {
                            pattern,
                            ty: None,
                            value,
                        },
                    });
                    params.push((name, pos));
                }
            }
        }
        match self.expr()? {
            Expr::Call(call) => Ok(Wrapper {
                call,
                pos,
                params,
                lead,
            }),
            other => Err(Error::new(
                other.pos(),
                "`with` passes the rest of the block to a call, and this is not one",
            )),
        }
    }
}
