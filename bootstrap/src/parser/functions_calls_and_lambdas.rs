//! Parses functions, calls, and lambdas: a function with its signature and
//! parameters, a function type, a call with its arguments, and a lambda.

use super::*;

impl Parser<'_> {
    /// Parses a function that starts at the token `from`. The errors of its
    /// statements are recorded, and a function with any gives `None`.
    ///
    /// A mistake in the signature of a function inside an implementation
    /// is given back, for the implementation to report. Elsewhere it is
    /// recorded, and the function is skipped.
    pub(super) fn function(
        &mut self,
        from: usize,
        nested: bool,
    ) -> Result<Option<Function>, Error> {
        let (name, pos, generics, params, result, mut result_name, effects, requires, ensures) =
            match self.body_signature() {
                Ok(signature) => signature,
                Err(e) if nested => return Err(e),
                Err(e) => {
                    self.errors.push(e);
                    self.skip_item(from);
                    return Ok(None);
                }
            };
        let mut block = Block::default();
        block.inner.append(&mut self.binds);
        let mut ok = true;
        let mut requires = requires;
        let mut ensures = ensures;
        let owed = self.bounds_owed(&params);
        ok &= owed.is_some();
        if let Some(mut owed) = owed {
            owed.append(&mut requires);
            requires = owed;
        }
        let given = self.result_owed(&params, result.as_ref(), &mut result_name);
        ok &= given.is_some();
        if let Some(mut given) = given {
            given.append(&mut ensures);
            ensures = given;
        }
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
                _ => match self.statement_into(&mut block) {
                    Ok(()) => {}
                    Err(e) if e.unbuilt => {
                        if !self.read_past_statement(start, e, &mut block) {
                            return Ok(None);
                        }
                    }
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
            result_name,
            effects,
            requires,
            needs: Vec::new(),
            ensures,
            body: block.finish(end),
            end,
        }))
    }

    /// Parses a function's signature, through the `{` that opens its body.
    pub(super) fn body_signature(&mut self) -> Result<BodySignature, Error> {
        self.bound_names_allowed = true;
        let signature = self.signature();
        self.bound_names_allowed = false;
        let (name, pos, generics, params, result, result_name, effects) = signature?;
        let (mut requires, mut ensures) = (Vec::new(), Vec::new());
        loop {
            self.skip_newlines();
            let keyword = match self.tok() {
                Tok::Keyword(k @ ("requires" | "ensures")) => *k,
                _ => break,
            };
            self.bump();
            let no_lambda = std::mem::replace(&mut self.no_lambda, true);
            let statement = self.expr();
            self.no_lambda = no_lambda;
            match keyword {
                "requires" => requires.push(statement?),
                _ => ensures.push(statement?),
            }
        }
        match self.tok() {
            Tok::Punct("{") => self.bump(),
            tok => return Err(self.expected("`{` to open the function's body", tok)),
        }
        Ok((
            name,
            pos,
            generics,
            params,
            result,
            result_name,
            effects,
            requires,
            ensures,
        ))
    }

    /// Parses a function's signature, through its list of effects.
    pub(super) fn signature(&mut self) -> Result<Signature, Error> {
        self.binds.clear();
        self.bump();
        let (name, pos) = self.defined("the function's name")?;
        let generics = self.generics()?;
        self.expect_punct("(", "`(`")?;
        let mut params = Vec::new();
        self.skip_newlines();
        loop {
            match self.tok() {
                Tok::Punct(")") => break,
                Tok::Ident(_) => params.push(self.param()?),
                tok if starts_pattern(tok) => params.push(self.param()?),
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
        let mut result_name = None;
        if self.tok() == &Tok::Punct("->") {
            self.bump();
            if let Tok::Ident(n) = self.tok()
                && self.next_tok() == &Tok::Punct(":")
            {
                result_name = Some((n.to_string(), self.pos()));
                self.bump();
                self.bump();
            }
            self.name_bound_ok = self.bound_names_allowed && self.tok() != &Tok::Keyword("fn");
            let ty = self.ty();
            self.name_bound_ok = false;
            result = Some(ty?);
        }
        let effects = self.effects()?;
        if self.tok() == &Tok::Newline
            && matches!(
                self.tokens.get(self.at + 1).map(|t| &t.tok),
                Some(Tok::Keyword("requires" | "ensures"))
            )
        {
            self.bump();
        }
        Ok((name, pos, generics, params, result, result_name, effects))
    }

    /// Parses `name: type`, and `= default` after it if it has one.
    pub(super) fn param(&mut self) -> Result<Param, Error> {
        let pattern = !matches!(self.tok(), Tok::Ident(_));
        let (name, pos, pattern) = if pattern {
            let pattern = self.binder()?;
            let (name, pos) = hidden(&pattern);
            (name, pos, Some(pattern))
        } else {
            let (name, pos) = self.defined("a parameter")?;
            (name, pos, None)
        };
        self.expect_punct(":", "`:` and the parameter's type")?;
        let take = self.taking();
        self.name_bound_ok = self.bound_names_allowed && self.tok() != &Tok::Keyword("fn");
        let (ty, takes) = self.ty_taking()?;
        self.name_bound_ok = false;
        let mut default = None;
        if self.tok() == &Tok::Punct("=") {
            if pattern.is_some() {
                return Err(Error::new(
                    self.pos(),
                    "a parameter written as a pattern has no name to leave out at a call, so it takes no default",
                )
                .with_fix("give the parameter a name and take it apart in the body"));
            }
            self.bump();
            self.skip_newlines();
            default = Some(self.default()?);
        }
        if let Some(pattern) = pattern {
            self.binds.push(taken_apart(pattern, &name, pos));
        }
        Ok(Param {
            name,
            pos,
            ty,
            default,
            take,
            takes,
        })
    }

    /// Parses a function type, from its `fn`: the types of its parameters in
    /// parentheses, each marked `take` if the function owns it, then
    /// `-> R` if it gives a result, and the effects it carries. It gives
    /// whether each parameter is marked `take`.
    pub(super) fn function_type(&mut self) -> Result<(Type, Vec<bool>), Error> {
        let pos = self.pos();
        self.bump();
        if self.tok() != &Tok::Punct("(") {
            return Err(self.expected("`(` and the function type's parameters", self.tok()));
        }
        let marked = self.list(|p| {
            let take = p.taking();
            Ok((take, p.ty()?))
        })?;
        let (takes, mut args): (Vec<bool>, Vec<Type>) = marked.into_iter().unzip();
        if self.tok() == &Tok::Punct("->") {
            self.bump();
            args.push(self.ty()?);
        } else {
            args.push(Type {
                name: String::new(),
                pos,
                args: Vec::new(),
                function: false,
                range: None,
            });
        }
        self.function_effects();
        let function = Type {
            name: String::new(),
            pos,
            args,
            function: true,
            range: None,
        };
        Ok((function, takes))
    }

    /// Parses what follows a name, which is at `pos` and after the name
    /// before the `.` if one is written: a call, if parentheses or a lambda
    /// follow it, and otherwise the name, or the field of the name before
    /// the `.`.
    pub(super) fn call_or_name(
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
            None => {
                if name == "_" {
                    self.holes.push(pos);
                }
                Expr::Name { name, pos }
            }
            Some((q, at)) => Expr::Field {
                of: Box::new(Expr::Name { name: q, pos: at }),
                name,
                pos,
            },
        })
    }

    /// Parses a lambda written after a call, if one is, as the call's last
    /// argument.
    pub(super) fn lambda_after(&mut self) -> Result<Option<Arg>, Error> {
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
    pub(super) fn lambda(&mut self) -> Result<Lambda, Error> {
        let pos = self.pos();
        let mut params = Vec::new();
        let mut types = Vec::new();
        let mut result = None;
        let mut binds = Vec::new();
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
                if starts_pattern(self.tok()) {
                    let pattern = self.binder()?;
                    let (name, pos) = hidden(&pattern);
                    binds.push(taken_apart(pattern, &name, pos));
                    params.push((name, pos));
                } else {
                    params.push(self.defined("a parameter's name")?);
                }
                types.push(if self.tok() == &Tok::Punct(":") {
                    self.bump();
                    Some(self.ty()?)
                } else {
                    None
                });
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
            if self.tok() == &Tok::Punct("->") {
                self.bump();
                result = Some(self.ty()?);
            }
        }
        self.expect_punct("{", "`{` to open the lambda's body")?;
        let no_lambda = std::mem::replace(&mut self.no_lambda, false);
        let body = self.block_rest(binds);
        self.no_lambda = no_lambda;
        let (body, end) = body?;
        Ok(Lambda {
            pos,
            params,
            types,
            result,
            body,
            end,
        })
    }

    /// Parses the arguments of a call, from its `(`.
    pub(super) fn call_rest(
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
    pub(super) fn args(&mut self, args: &mut Vec<Arg>) -> Result<(), Error> {
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

    pub(super) fn arg(&mut self) -> Result<Arg, Error> {
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
