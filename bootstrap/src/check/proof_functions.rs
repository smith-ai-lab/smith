//! Proof functions: a function whose result is a statement, which is checked
//! and never built.

use super::*;

impl<'a> Checker<'a> {
    /// Checks a proof function: its statement is a `bool` over its
    /// parameters. Its body is a proof, which the kernel checks.
    pub(super) fn proof_function(&mut self, f: usize) {
        let defs = self.defs;
        let (def, sig) = (&defs.file.functions[f], &defs.signatures[f]);
        let Some(statement) = def.result.as_ref().and_then(|t| t.statement.as_deref()) else {
            return;
        };
        let mut body = Body {
            f: Some(def),
            default: None,
            scope: &sig.scope,
            // A proof is never run, so the memory of what it names costs nothing.
            listed: vec!["alloc"],
            extra: Vec::new(),
            unknown_effect: false,
            missing: Vec::new(),
            names: sig.params.clone(),
            params: sig.params.len(),
            vars: Vec::new(),
            numbers: Numbers::Off,
            decided: HashMap::new(),
            facts: proof::Facts::default(),
            tails: Vec::new(),
            gives: Ty::Nothing,
            contract: false,
            loops: 0,
            outer_loops: 0,
            returns: Ty::Nothing,
            returned: None,
            lambdas: 0,
        };
        // What the proof's caller owes is what it knows.
        for requirement in &def.requires {
            self.assume(&mut body, requirement, true);
        }
        // A statement is exact arithmetic that is never worked out, so no sum
        // in it is asked to fit.
        body.contract = true;
        self.condition(&mut body, statement);
        body.contract = false;
        if !def.effects.is_empty() {
            self.errors.push(Error::new(
                def.pos,
                format!("`{}` is a proof, which carries no effect", def.name),
            ));
        }
        self.proof_block(&mut body, &def.body, &mut Vec::new());
    }
}

impl<'a> Checker<'a> {
    /// Checks the steps of a proof: `same`, `arith` of the proofs named so far,
    /// a name given by `unfold` and the steps after it, or an `if` whose blocks
    /// are steps.
    fn proof_block(&mut self, body: &mut Body<'a>, stmts: &'a [Stmt], names: &mut Vec<String>) {
        match stmts {
            [Stmt::Expr(Expr::Name { name, .. })] if name == "same" => {}
            [Stmt::Expr(Expr::Call(c))] if c.callee == "arith" && c.qualifier.is_none() => {
                for arg in &c.args {
                    match &arg.value {
                        Expr::Name { name, .. } if names.contains(name) => {}
                        other => self.errors.push(Error::new(
                            other.pos(),
                            "`arith` is handed the names of proofs given before it",
                        )),
                    }
                }
            }
            [Stmt::Expr(Expr::Match(m))] if matches!(m.form, Form::If { otherwise: true }) => {
                self.condition(body, &m.of);
                let known = body.facts.mark();
                for (k, arm) in m.arms.iter().enumerate() {
                    self.assume(body, &m.of, k == 0);
                    let mark = names.len();
                    self.proof_block(body, &arm.body, names);
                    names.truncate(mark);
                    body.facts.reset(&known);
                }
            }
            [
                Stmt::Define {
                    name,
                    value: Expr::Call(c),
                    var: false,
                    ..
                },
                rest @ ..,
            ] if c.callee == "unfold" && c.qualifier.is_none() => {
                match c.args.as_slice() {
                    [arg] if matches!(arg.value, Expr::Call(_)) => {
                        self.expr(body, &arg.value, &Want::Any);
                    }
                    _ => self.errors.push(Error::new(
                        c.pos,
                        "`unfold` is of one call of a function, as `unfold(f(x))`",
                    )),
                }
                names.push(name.clone());
                self.proof_block(body, rest, names);
            }
            [
                Stmt::Define {
                    name,
                    value: value @ Expr::Call(c),
                    var: false,
                    ..
                },
                rest @ ..,
            ] if self.proof_called(c).is_some() => {
                // A call of a proof function is a proof of its statement for
                // the arguments, and is checked as any call is, so a proof that
                // calls itself is held to its `decreases`.
                self.expr(body, value, &Want::Any);
                names.push(name.clone());
                self.proof_block(body, rest, names);
            }
            [first, ..] => self.errors.push(Error::new(
                stmt_pos(first),
                "this is not a step of a proof: `same`, `arith`, a name given by `unfold`, or an `if` of steps",
            )),
            [] => self.errors.push(Error::new(
                body.f.map_or(Pos::START, |f| f.pos),
                "a proof has no step",
            )),
        }
    }
}

impl<'a> Checker<'a> {
    /// The proof function a call is of, if it is one.
    pub(super) fn proof_called(&self, c: &Call) -> Option<usize> {
        if c.qualifier.is_some() {
            return None;
        }
        let f = self.defs.function(c.name_pos, &c.callee)?;
        is_proof(&self.defs.file.functions[f]).then_some(f)
    }
}

fn stmt_pos(s: &Stmt) -> Pos {
    match s {
        Stmt::Define { pos, .. } | Stmt::Assign { pos, .. } => *pos,
        Stmt::Destructure { pattern, .. } => pattern.pos(),
        Stmt::Expr(e) => e.pos(),
    }
}
