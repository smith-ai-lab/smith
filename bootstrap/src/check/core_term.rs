//! The core term of the functions of the file that are made of whole numbers
//! and `bool`: a number, a name, `+`, `-`, `*`, the six comparisons, `and`,
//! `or`, `not`, `if`, a binding, and a call of a function of the file that is
//! itself one. A function that has anything else, or that has a type
//! parameter, has no core term yet.

use super::*;
use crate::core::{self, Callee, Kind, Lit, Make, Operator, Pattern as CorePattern, Term};

/// The case of the prelude's `bool` that `name` is.
fn case_of(name: &str) -> Option<usize> {
    match name {
        "false" => Some(0),
        "true" => Some(1),
        _ => None,
    }
}

impl<'a> Typed<'a> {
    /// The core term of each function of the file that has one, in the order
    /// they are written.
    pub(super) fn core_terms(&self) -> Vec<core::Function> {
        let file = self.defs.file;
        let mut found = Vec::new();
        for (place, f) in file.functions.iter().enumerate() {
            if module_of(f.pos) != 0 || of_prelude(f.pos) || !f.generics.is_empty() {
                continue;
            }
            let sig = &self.defs.signatures[place];
            let params: Option<Vec<(String, core::Type)>> = sig
                .params
                .iter()
                .map(|(n, t)| Some((n.clone(), self.core_type(t)?)))
                .collect();
            let (Some(params), Some(body)) = (params, self.core_block(&f.body)) else {
                continue;
            };
            found.push(core::Function {
                name: f.name.clone(),
                place,
                params,
                body,
            });
        }
        found
    }

    fn core_type(&self, ty: &Ty) -> Option<core::Type> {
        Some(match ty {
            Ty::Text => core::Type::Text,
            Ty::Int(i) => core::Type::int(i.min(), i.max()),
            Ty::Enum(e, args) if args.is_empty() => core::Type::Enum(*e, Vec::new()),
            Ty::Nothing => core::Type::Nothing,
            _ => return None,
        })
    }

    fn core_of(&self, e: &Expr) -> Option<core::Type> {
        self.core_type(self.types.get(&addr(e))?)
    }

    fn core_bool(&self) -> core::Type {
        core::Type::Enum(self.defs.boolean, Vec::new())
    }

    fn core_truth(&self, value: bool, pos: Pos) -> Term {
        Term::new(
            Kind::Make(
                Make::Case(self.defs.boolean, usize::from(value)),
                Vec::new(),
            ),
            self.core_bool(),
            pos,
        )
    }

    /// The statements of a block: names given values, and then the value it
    /// gives.
    fn core_block(&self, stmts: &[Stmt]) -> Option<Term> {
        match stmts {
            [Stmt::Expr(e)] => self.core_expr(e),
            [
                Stmt::Define {
                    name,
                    pos,
                    var: false,
                    value,
                    ..
                },
                rest @ ..,
            ] => {
                let value = self.core_expr(value)?;
                let rest = self.core_block(rest)?;
                let ty = rest.ty.clone();
                Some(Term::new(
                    Kind::Bind {
                        name: name.clone(),
                        value: Box::new(value),
                        rest: Box::new(rest),
                    },
                    ty,
                    *pos,
                ))
            }
            _ => None,
        }
    }

    fn core_call(
        &self,
        op: Operator,
        a: &Expr,
        b: &Expr,
        ty: core::Type,
        pos: Pos,
    ) -> Option<Term> {
        Some(Term::new(
            Kind::Call {
                callee: Callee::Operator(op),
                args: vec![self.core_expr(a)?, self.core_expr(b)?],
            },
            ty,
            pos,
        ))
    }

    /// A choice between `then` and `otherwise` by a `bool`.
    fn core_choice(&self, of: Term, then: Term, otherwise: Term) -> Term {
        let (ty, pos) = (then.ty.clone(), of.pos);
        Term::new(
            Kind::Choose {
                of: Box::new(of),
                arms: vec![
                    (CorePattern::Case(1, None), then),
                    (CorePattern::Case(0, None), otherwise),
                ],
            },
            ty,
            pos,
        )
    }

    fn core_expr(&self, e: &Expr) -> Option<Term> {
        let ty = self.core_of(e)?;
        match e {
            Expr::Number {
                magnitude,
                negative,
                pos,
                ..
            } => {
                let n = i128::try_from(*magnitude).ok()?;
                let n = if *negative { -n } else { n };
                Some(Term::new(Kind::Lit(Lit::Int(n)), ty, *pos))
            }
            Expr::Name { name, pos }
            | Expr::Case {
                name,
                pos,
                payload: None,
            } => match case_of(name) {
                Some(c) if ty == self.core_bool() => Some(self.core_truth(c == 1, *pos)),
                _ => matches!(e, Expr::Name { .. })
                    .then(|| Term::new(Kind::Name(name.clone()), ty, *pos)),
            },
            Expr::Arith {
                op,
                left,
                right,
                op_pos,
            } => {
                let op = match op {
                    ArithOp::Add => Operator::Add,
                    ArithOp::Sub => Operator::Sub,
                    ArithOp::Mul => Operator::Mul,
                    _ => return None,
                };
                self.core_call(op, left, right, ty, *op_pos)
            }
            Expr::Call(call) => self.core_named_call(call, ty),
            Expr::Match(m) => self.core_match(m, ty),
            _ => None,
        }
    }

    /// `a == b`, or a call of a function of the file.
    fn core_named_call(&self, call: &Call, ty: core::Type) -> Option<Term> {
        if call.qualifier.as_ref().is_some_and(|(q, _)| q == "Eq")
            && call.callee == "eq"
            && call.args.len() == 2
        {
            return self.core_call(
                Operator::Equal,
                &call.args[0].value,
                &call.args[1].value,
                ty,
                call.pos,
            );
        }
        if call.qualifier.is_some() || call.args.iter().any(|a| a.name.is_some() || a.after) {
            return None;
        }
        let f = self.defs.function(call.name_pos, &call.callee)?;
        let (def, sig) = (&self.defs.file.functions[f], &self.defs.signatures[f]);
        if module_of(def.pos) != 0 || of_prelude(def.pos) || !def.generics.is_empty() {
            return None;
        }
        if call.args.len() != sig.params.len() {
            return None;
        }
        let args: Option<Vec<Term>> = call.args.iter().map(|a| self.core_expr(&a.value)).collect();
        Some(Term::new(
            Kind::Call {
                callee: Callee::Function(f),
                args: args?,
            },
            ty,
            call.pos,
        ))
    }

    /// `and`, `or`, `not`, `if`, and a comparison, which is written as a
    /// `match` on `Ord.compare`.
    fn core_match(&self, m: &crate::parser::Match, ty: core::Type) -> Option<Term> {
        let side = |arm: usize| match m.arms.get(arm)?.body.as_slice() {
            body @ [_, ..] => self.core_block(body),
            [] => None,
        };
        let bool_ty = self.core_bool();
        match m.form {
            Form::Not => {
                let of = self.core_expr(&m.of)?;
                let (f, t) = (self.core_truth(false, m.pos), self.core_truth(true, m.pos));
                Some(self.core_choice(of, f, t))
            }
            Form::And => {
                let (of, b) = (self.core_expr(&m.of)?, side(0)?);
                let f = self.core_truth(false, m.pos);
                Some(self.core_choice(of, b, f))
            }
            Form::Or => {
                let (of, b) = (self.core_expr(&m.of)?, side(1)?);
                let t = self.core_truth(true, m.pos);
                Some(self.core_choice(of, t, b))
            }
            Form::If { otherwise: true } => {
                let of = self.core_expr(&m.of)?;
                let (then, otherwise) = (side(0)?, side(1)?);
                let mut choice = self.core_choice(of, then, otherwise);
                choice.ty = ty;
                Some(choice)
            }
            Form::Match => {
                let Expr::Call(c) = &m.of else {
                    return None;
                };
                if !(c.qualifier.as_ref().is_some_and(|(q, _)| q == "Ord")
                    && c.callee == "compare"
                    && c.args.len() == 2
                    && ty == bool_ty)
                {
                    return None;
                }
                let holds = |case: &str| {
                    m.arms.iter().any(|a| {
                        matches!(&a.pattern, crate::parser::Pattern::Case { name, .. } if name == case)
                            && matches!(a.body.as_slice(),
                                [Stmt::Expr(Expr::Name { name, .. })] if name == "true")
                    })
                };
                let op = match (holds("less"), holds("equal"), holds("greater")) {
                    (true, false, false) => Operator::Less,
                    (true, true, false) => Operator::LessEqual,
                    (false, false, true) => Operator::Greater,
                    (false, true, true) => Operator::GreaterEqual,
                    _ => return None,
                };
                self.core_call(op, &c.args[0].value, &c.args[1].value, ty, m.pos)
            }
            _ => None,
        }
    }
}
