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
            Ty::Enum(e, args) => core::Type::Enum(*e, self.core_types(args)?),
            Ty::Struct(s, args) => core::Type::Struct(*s, self.core_types(args)?),
            Ty::Tuple(items) => core::Type::Tuple(self.core_types(items)?),
            Ty::Nothing => core::Type::Nothing,
            _ => return None,
        })
    }

    fn core_types(&self, tys: &[Ty]) -> Option<Vec<core::Type>> {
        tys.iter().map(|t| self.core_type(t)).collect()
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
            Expr::Text { text, pos } => {
                Some(Term::new(Kind::Lit(Lit::Text(text.clone())), ty, *pos))
            }
            Expr::Tuple { items, pos } => {
                let items: Option<Vec<Term>> = items.iter().map(|i| self.core_expr(i)).collect();
                Some(Term::new(Kind::Make(Make::Tuple, items?), ty, *pos))
            }
            Expr::Case {
                name,
                pos,
                payload: Some(payload),
            } => self.core_case(name, *pos, Some(payload), ty),
            Expr::Field { of, name, pos } => self.core_field(of, name, *pos, ty),
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
        if let Some(s) = (call.qualifier.is_none())
            .then(|| self.defs.struct_named(&call.callee))
            .flatten()
        {
            return self.core_struct(call, s, ty);
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
                    return self.core_choose(m, ty);
                };
                if !(c.qualifier.as_ref().is_some_and(|(q, _)| q == "Ord")
                    && c.callee == "compare"
                    && c.args.len() == 2
                    && ty == bool_ty)
                {
                    return self.core_choose(m, ty);
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

impl<'a> Typed<'a> {
    /// A case of an enum, with what it holds.
    fn core_case(
        &self,
        name: &str,
        pos: Pos,
        payload: Option<&Expr>,
        ty: core::Type,
    ) -> Option<Term> {
        let core::Type::Enum(e, _) = &ty else {
            return None;
        };
        let case = self.defs.case_of(*e, name)?;
        let args = match payload {
            Some(p) => vec![self.core_expr(p)?],
            None => Vec::new(),
        };
        Some(Term::new(Kind::Make(Make::Case(*e, case), args), ty, pos))
    }

    /// `p.x`, a field of a struct or a tuple, or `Pet.none`, which names a
    /// case in full.
    fn core_field(&self, of: &Expr, name: &str, pos: Pos, ty: core::Type) -> Option<Term> {
        if !self.types.contains_key(&addr(of)) {
            let Expr::Name {
                name: enum_name, ..
            } = of
            else {
                return None;
            };
            self.defs.enum_named(enum_name)?;
            return self.core_case(name, pos, None, ty);
        }
        let of_term = self.core_expr(of)?;
        let index = match &of_term.ty {
            core::Type::Struct(s, _) => self.defs.file.structs[*s]
                .fields
                .iter()
                .position(|f| f.name == name)?,
            core::Type::Tuple(_) => name.parse().ok()?,
            _ => return None,
        };
        Some(Term::new(
            Kind::Field {
                of: Box::new(of_term),
                name: name.to_string(),
                index,
            },
            ty,
            pos,
        ))
    }

    /// A struct written out: `P(x: 1, y: 2)`, with the fields it leaves out
    /// given their defaults.
    fn core_struct(&self, call: &Call, s: usize, ty: core::Type) -> Option<Term> {
        let def = &self.defs.file.structs[s];
        let mut fields: Vec<Option<Term>> = vec![None; def.fields.len()];
        for arg in &call.args {
            let (name, _) = arg.name.as_ref()?;
            let at = def.fields.iter().position(|f| &f.name == name)?;
            fields[at] = Some(self.core_expr(&arg.value)?);
        }
        for (at, default) in self.defs.left_out(call) {
            fields[at] = Some(self.core_expr(default)?);
        }
        let args: Option<Vec<Term>> = fields.into_iter().collect();
        Some(Term::new(Kind::Make(Make::Struct(s), args?), ty, call.pos))
    }

    /// A pattern, for a value of type `ty`.
    fn core_pattern(&self, p: &crate::parser::Pattern, ty: &Ty) -> Option<CorePattern> {
        use crate::parser::Pattern as P;
        Some(match p {
            P::Bind {
                name, var: None, ..
            } if name == "_" => CorePattern::Any,
            P::Bind {
                name, var: None, ..
            } => CorePattern::Bind(name.clone()),
            P::Bind { var: Some(_), .. } => return None,
            P::Number {
                magnitude,
                negative,
                ..
            } => {
                let n = i128::try_from(*magnitude).ok()?;
                CorePattern::Lit(Lit::Int(if *negative { -n } else { n }))
            }
            P::Text { text, .. } => CorePattern::Lit(Lit::Text(text.clone())),
            P::Case { name, inner, .. } => {
                let Ty::Enum(e, args) = ty else {
                    return None;
                };
                let case = self.defs.case_of(*e, name)?;
                let held = self.defs.cases[*e][case].as_ref();
                let inner = match (inner, held) {
                    (Some(p), Some(held)) => {
                        let held_ty = held.subst(args);
                        Some(Box::new(self.core_pattern(p, &held_ty)?))
                    }
                    (None, _) => None,
                    _ => return None,
                };
                CorePattern::Case(case, inner)
            }
            P::Tuple { items, .. } => {
                let Ty::Tuple(tys) = ty else {
                    return None;
                };
                if items.len() != tys.len() {
                    return None;
                }
                CorePattern::Tuple(
                    items
                        .iter()
                        .zip(tys)
                        .map(|(p, t)| self.core_pattern(p, t))
                        .collect::<Option<_>>()?,
                )
            }
        })
    }

    /// A `match` on any value, by its patterns.
    fn core_choose(&self, m: &crate::parser::Match, ty: core::Type) -> Option<Term> {
        let of_ty = self.types.get(&addr(&m.of))?;
        let of = self.core_expr(&m.of)?;
        let arms: Option<Vec<(CorePattern, Term)>> = m
            .arms
            .iter()
            .map(|a| {
                Some((
                    self.core_pattern(&a.pattern, of_ty)?,
                    self.core_block(&a.body)?,
                ))
            })
            .collect();
        let pos = of.pos;
        Some(Term::new(
            Kind::Choose {
                of: Box::new(of),
                arms: arms?,
            },
            ty,
            pos,
        ))
    }
}
