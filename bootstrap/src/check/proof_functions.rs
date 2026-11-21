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
            listed: Vec::new(),
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
            contract: true,
            loops: 0,
            outer_loops: 0,
            returns: Ty::Nothing,
            returned: None,
            lambdas: 0,
        };
        self.condition(&mut body, statement);
        if !def.effects.is_empty() {
            self.errors.push(Error::new(
                def.pos,
                format!("`{}` is a proof, which carries no effect", def.name),
            ));
        }
        self.errors.push(Error::unsupported(
            def.body.first().map_or(def.pos, stmt_pos),
            "a proof body",
        ));
    }
}

fn stmt_pos(s: &Stmt) -> Pos {
    match s {
        Stmt::Define { pos, .. } | Stmt::Assign { pos, .. } => *pos,
        Stmt::Destructure { pattern, .. } => pattern.pos(),
        Stmt::Expr(e) => e.pos(),
    }
}
