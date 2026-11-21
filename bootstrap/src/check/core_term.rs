//! The core term of the functions of the file that are made of whole numbers
//! and `bool`: a number, a name, `+`, `-`, `*`, the six comparisons, `and`,
//! `or`, `not`, `if`, a binding, and a call of a function of the file that is
//! itself one. A function that has anything else, or that has a type
//! parameter, has no core term yet.

use super::*;
use crate::core::{self, Callee, Kind, Lit, Make, Operator, Pattern as CorePattern, Source, Term};

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
            if of_prelude(f.pos) || !f.generics.is_empty() {
                continue;
            }
            self.core_vars.borrow_mut().clear();
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
            Ty::List(of) => core::Type::List(Box::new(self.core_type(of.first()?)?)),
            Ty::Slice(of) => core::Type::Slice(Box::new(self.core_type(of.first()?)?)),
            Ty::Fn(of) => {
                let (result, params) = of.split_last()?;
                core::Type::Fn(self.core_types(params)?, Box::new(self.core_type(result)?))
            }
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
    /// gives. A name that is a `var` of a block around this one is not
    /// given a value here, as no node assigns.
    fn core_block(&self, stmts: &[Stmt]) -> Option<Term> {
        let mark = self.core_vars.borrow().len();
        let term = self.core_stmts(stmts, &mut Vec::new());
        self.core_vars.borrow_mut().truncate(mark);
        term
    }

    /// The statements of a block, where `own` are the `var`s it may give a
    /// value to again, as a name is bound again.
    fn core_stmts(&self, stmts: &[Stmt], own: &mut Vec<String>) -> Option<Term> {
        match stmts {
            [Stmt::Expr(e)] => self.core_expr(e),
            [Stmt::Expr(Expr::Loop(l)), rest @ ..] if !rest.is_empty() => {
                let (looped, names) = self.core_loop(l)?;
                // The loop gives the values the `var`s it assigns end with.
                let rest = self.core_stmts(rest, own)?;
                let ty = rest.ty.clone();
                let pos = looped.pos;
                let held = "loop state".to_string();
                let mut term = rest;
                for (index, (name, var_ty)) in names.iter().enumerate().rev() {
                    let field = Term::new(
                        Kind::Field {
                            of: Box::new(Term::new(
                                Kind::Name(held.clone()),
                                looped.ty.clone(),
                                pos,
                            )),
                            name: index.to_string(),
                            index,
                        },
                        var_ty.clone(),
                        pos,
                    );
                    term = Term::new(
                        Kind::Bind {
                            name: name.clone(),
                            value: Box::new(field),
                            rest: Box::new(term),
                        },
                        ty.clone(),
                        pos,
                    );
                }
                Some(Term::new(
                    Kind::Bind {
                        name: held,
                        value: Box::new(looped),
                        rest: Box::new(term),
                    },
                    ty,
                    pos,
                ))
            }
            [
                Stmt::Define {
                    name,
                    pos,
                    var,
                    value,
                    ty: _,
                },
                rest @ ..,
            ] => {
                let known = self.core_vars.borrow().iter().any(|(n, _)| n == name);
                if !var && known && !own.contains(name) {
                    return None;
                }
                let value = self.core_expr(value)?;
                if *var {
                    own.push(name.clone());
                    self.core_vars
                        .borrow_mut()
                        .push((name.clone(), value.ty.clone()));
                }
                let rest = self.core_stmts(rest, own)?;
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

    /// A `loop`, and the `var`s it assigns, in the order they are declared,
    /// with their types, which are the state it hands from round to round.
    fn core_loop(&self, l: &crate::parser::Loop) -> Option<(Term, Vec<(String, core::Type)>)> {
        let (source, counter) = match (&l.range, &l.over) {
            (Some(r), None) => (
                Source::Range(
                    Box::new(self.core_expr(&r.from)?),
                    Box::new(self.core_expr(&r.to)?),
                ),
                r.name.clone(),
            ),
            (None, Some(o)) => (
                Source::Over(Box::new(self.core_expr(&o.value)?)),
                o.name.clone(),
            ),
            (None, None) => (Source::Open, "_".to_string()),
            _ => return None,
        };
        let assigned = assigned_in(&l.body);
        let state: Vec<(String, core::Type)> = self
            .core_vars
            .borrow()
            .iter()
            .filter(|(n, _)| assigned.contains(n))
            .cloned()
            .collect();
        let pos = l.pos;
        let initial: Vec<(String, Term)> = state
            .iter()
            .map(|(n, t)| (n.clone(), Term::new(Kind::Name(n.clone()), t.clone(), pos)))
            .collect();
        let mark = self.core_vars.borrow().len();
        let body = self.core_round(&l.body, &state, pos);
        self.core_vars.borrow_mut().truncate(mark);
        let ty = core::Type::Tuple(state.iter().map(|(_, t)| t.clone()).collect());
        Some((
            Term::new(
                Kind::Loop {
                    source,
                    counter,
                    state: initial,
                    body: Box::new(body?),
                },
                ty,
                pos,
            ),
            state,
        ))
    }

    /// The statements of a round: each `var` of the state it gives a value
    /// is bound again, and the round ends in a `Next`, or in a `Done` where
    /// an `if` with no `else` ends in `break`.
    fn core_round(&self, stmts: &[Stmt], state: &[(String, core::Type)], pos: Pos) -> Option<Term> {
        let end = |done: bool| {
            let values: Vec<Term> = state
                .iter()
                .map(|(n, t)| Term::new(Kind::Name(n.clone()), t.clone(), pos))
                .collect();
            Term::new(
                if done {
                    Kind::Done(values)
                } else {
                    Kind::Next(values)
                },
                core::Type::Nothing,
                pos,
            )
        };
        let own: Vec<String> = state.iter().map(|(n, _)| n.clone()).collect();
        match stmts {
            [] => Some(end(false)),
            [Stmt::Expr(Expr::Break { .. }), ..] => Some(end(true)),
            [Stmt::Expr(Expr::Match(m)), rest @ ..]
                if matches!(m.form, Form::If { otherwise: false }) =>
            {
                // `if c { ...; break }`, and then the rest of the round.
                let [then, _] = m.arms.as_slice() else {
                    return None;
                };
                if !matches!(then.body.last(), Some(Stmt::Expr(Expr::Break { .. }))) {
                    return None;
                }
                let of = self.core_expr(&m.of)?;
                let stop = self.core_round(&then.body, state, pos)?;
                let go = self.core_round(rest, state, pos)?;
                Some(Term::new(
                    Kind::Choose {
                        of: Box::new(of),
                        arms: vec![
                            (CorePattern::Case(1, None), stop),
                            (CorePattern::Case(0, None), go),
                        ],
                    },
                    core::Type::Nothing,
                    pos,
                ))
            }
            [
                Stmt::Define {
                    name,
                    pos: at,
                    var: false,
                    value,
                    ..
                },
                rest @ ..,
            ] if own.contains(name) || !self.core_vars.borrow().iter().any(|(n, _)| n == name) => {
                let value = self.core_expr(value)?;
                let rest = self.core_round(rest, state, pos)?;
                Some(Term::new(
                    Kind::Bind {
                        name: name.clone(),
                        value: Box::new(value),
                        rest: Box::new(rest),
                    },
                    core::Type::Nothing,
                    *at,
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
            Expr::List { items, pos } => {
                let items: Option<Vec<Term>> = items.iter().map(|i| self.core_expr(i)).collect();
                Some(Term::new(Kind::Make(Make::List, items?), ty, *pos))
            }
            Expr::Lambda(l) => self.core_lambda(l, ty),
            Expr::Index { of, index, pos } => {
                let (of, index) = (self.core_expr(of)?, self.core_expr(index)?);
                Some(Term::new(
                    Kind::Call {
                        callee: Callee::Operator(Operator::Index),
                        args: vec![of, index],
                    },
                    ty,
                    *pos,
                ))
            }
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
        if call.qualifier.is_none() && self.value_calls.contains(&addr(call)) {
            let args: Vec<Term> = call
                .args
                .iter()
                .map(|a| self.core_expr(&a.value))
                .collect::<Option<_>>()?;
            // The function is a name that holds one, whose type is what the
            // arguments and the result say.
            let held = core::Type::Fn(
                args.iter().map(|a| a.ty.clone()).collect(),
                Box::new(ty.clone()),
            );
            let function = Term::new(Kind::Name(call.callee.clone()), held, call.name_pos);
            return Some(Term::new(
                Kind::Apply {
                    function: Box::new(function),
                    args,
                },
                ty,
                call.pos,
            ));
        }
        if call.qualifier.is_some() || call.args.iter().any(|a| a.name.is_some() || a.after) {
            return None;
        }
        let f = self.defs.function(call.name_pos, &call.callee)?;
        let (def, sig) = (&self.defs.file.functions[f], &self.defs.signatures[f]);
        if of_prelude(def.pos) || !def.generics.is_empty() {
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
        if matches!(
            of_term.ty,
            core::Type::List(_) | core::Type::Slice(_) | core::Type::Text
        ) && matches!(name, "size" | "bytes")
        {
            let op = if name == "size" {
                Operator::Size
            } else {
                Operator::Bytes
            };
            return Some(Term::new(
                Kind::Call {
                    callee: Callee::Operator(op),
                    args: vec![of_term],
                },
                ty,
                pos,
            ));
        }
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

impl<'a> Typed<'a> {
    /// A function written where it is used, whose type is the one it is
    /// given and whose parameters have the types that type says.
    fn core_lambda(&self, l: &crate::parser::Lambda, ty: core::Type) -> Option<Term> {
        let core::Type::Fn(params, _) = &ty else {
            return None;
        };
        if params.len() != l.params.len() {
            return None;
        }
        let params: Vec<(String, core::Type)> = l
            .params
            .iter()
            .zip(params)
            .map(|(n, t)| (n.0.clone(), t.clone()))
            .collect();
        let body = self.core_block(&l.body)?;
        Some(Term::new(
            Kind::Lambda {
                params,
                body: Box::new(body),
            },
            ty,
            l.pos,
        ))
    }
}

/// The names a block gives a value to again, where it is an assignment of a
/// `var` that the block does not declare, at the top of the block and in an
/// `if` with no `else` that is there.
fn assigned_in(stmts: &[Stmt]) -> Vec<String> {
    let mut found = Vec::new();
    for stmt in stmts {
        match stmt {
            Stmt::Define {
                name, var: false, ..
            } => found.push(name.clone()),
            Stmt::Expr(Expr::Match(m)) if matches!(m.form, Form::If { otherwise: false }) => {
                for arm in &m.arms {
                    found.extend(assigned_in(&arm.body));
                }
            }
            _ => {}
        }
    }
    found
}

/// How many steps the kernel works out a requirement for.
const STEPS: u64 = 1_000_000;

impl<'a> Typed<'a> {
    /// Works out each requirement that `arith` does not read, whose arguments
    /// are all known, and gives the errors of those that are false, did not
    /// end, or are not terms the kernel works out.
    pub(super) fn prove_by_kernel(&self, calls: &[proof::KernelCall]) -> Vec<Error> {
        if calls.is_empty() {
            return Vec::new();
        }
        let all = self.core_terms();
        let mut errors = Vec::new();
        for pending in calls {
            let mut machine = crate::kernel::Machine::new(&all, STEPS);
            let message = match machine.call(pending.predicate, pending.args.clone()) {
                Ok((crate::kernel::Value::Case(1, _), _)) => continue,
                Ok((_, why)) => match why {
                    Some(why) => format!(
                        "`{}` is false: `{}` at {}:{} was {}",
                        pending.written, why.comparison, why.pos.line, why.pos.col, why.holds
                    ),
                    None => match machine.last_comparison() {
                        Some(last) => format!(
                            "`{}` is false: its last comparison, `{}` at {}:{}, was {}",
                            pending.written,
                            last.comparison,
                            last.pos.line,
                            last.pos.col,
                            last.holds
                        ),
                        None => format!("`{}` is false", pending.written),
                    },
                },
                Err(crate::kernel::Failure::Limit(n)) => {
                    format!("`{}` did not finish within {n} steps", pending.written)
                }
                Err(crate::kernel::Failure::Stuck(what)) => format!(
                    "cannot prove that `{}`: the kernel does not work out {what}",
                    pending.written
                ),
            };
            errors.push(
                Error::new(pending.pos, message).with_fix(
                    "check the arguments first with an `if`, or state it in a `requires`",
                ),
            );
        }
        errors
    }
}

impl<'a> Typed<'a> {
    /// The steps of a proof as a term: `same`, or an `if` that splits the
    /// proof into cases.
    fn core_proof(&self, stmts: &[Stmt]) -> Option<Term> {
        match stmts {
            [Stmt::Expr(Expr::Name { name, pos })] if name == "same" => Some(Term::new(
                Kind::Call {
                    callee: Callee::Step(core::Step::Same),
                    args: Vec::new(),
                },
                core::Type::Nothing,
                *pos,
            )),
            [Stmt::Expr(Expr::Call(c))] if c.callee == "arith" && c.qualifier.is_none() => {
                let args: Option<Vec<Term>> = c
                    .args
                    .iter()
                    .map(|a| match &a.value {
                        Expr::Name { name, pos } => Some(Term::new(
                            Kind::Name(name.clone()),
                            core::Type::Nothing,
                            *pos,
                        )),
                        _ => None,
                    })
                    .collect();
                Some(Term::new(
                    Kind::Call {
                        callee: Callee::Step(core::Step::Arith),
                        args: args?,
                    },
                    core::Type::Nothing,
                    c.pos,
                ))
            }
            [
                Stmt::Define {
                    name,
                    pos,
                    value: Expr::Call(c),
                    var: false,
                    ..
                },
                rest @ ..,
            ] if c.callee == "unfold" && c.qualifier.is_none() => {
                let [arg] = c.args.as_slice() else {
                    return None;
                };
                let call = self.core_expr(&arg.value)?;
                let value = Term::new(
                    Kind::Call {
                        callee: Callee::Step(core::Step::Unfold),
                        args: vec![call],
                    },
                    core::Type::Nothing,
                    c.pos,
                );
                let rest = self.core_proof(rest)?;
                Some(Term::new(
                    Kind::Bind {
                        name: name.clone(),
                        value: Box::new(value),
                        rest: Box::new(rest),
                    },
                    core::Type::Nothing,
                    *pos,
                ))
            }
            [Stmt::Expr(Expr::Match(m))] if matches!(m.form, Form::If { otherwise: true }) => {
                let of = self.core_expr(&m.of)?;
                let [then, otherwise] = m.arms.as_slice() else {
                    return None;
                };
                let (then, otherwise) = (
                    self.core_proof(&then.body)?,
                    self.core_proof(&otherwise.body)?,
                );
                Some(Term::new(
                    Kind::Choose {
                        of: Box::new(of),
                        arms: vec![
                            (CorePattern::Case(1, None), then),
                            (CorePattern::Case(0, None), otherwise),
                        ],
                    },
                    core::Type::Nothing,
                    m.pos,
                ))
            }
            _ => None,
        }
    }

    /// Checks every proof function of the file with the kernel, and gives the
    /// errors of those that do not prove their statement.
    pub(super) fn prove_proofs(&self) -> Vec<Error> {
        let proofs: Vec<(usize, &Arc<Function>)> = self
            .defs
            .file
            .functions
            .iter()
            .enumerate()
            .filter(|(_, f)| is_proof(f) && !of_prelude(f.pos))
            .collect();
        if proofs.is_empty() {
            return Vec::new();
        }
        let all = self.core_terms();
        let mut errors = Vec::new();
        for (place, f) in proofs {
            let Some(statement_expr) = f.result.as_ref().and_then(|t| t.statement.as_deref())
            else {
                continue;
            };
            let sig = &self.defs.signatures[place];
            let params: Option<Vec<(String, core::Type)>> = sig
                .params
                .iter()
                .map(|(n, t)| Some((n.clone(), self.core_type(t)?)))
                .collect();
            let (Some(params), Some(statement), Some(body)) = (
                params,
                self.core_expr(statement_expr),
                self.core_proof(&f.body),
            ) else {
                errors.push(Error::new(
                    f.pos,
                    format!(
                        "the kernel does not read the statement or the steps of `{}`",
                        f.name
                    ),
                ));
                continue;
            };
            let at = f.body.first().map_or(f.pos, |s| match s {
                Stmt::Expr(e) => e.pos(),
                _ => f.pos,
            });
            let proof = core::Proof {
                name: f.name.clone(),
                pos: at,
                params,
                statement,
                written: proof::written_contract(&|_| None, statement_expr),
                body,
            };
            if let Err(message) = crate::kernel::check_proof(&all, &proof, STEPS) {
                errors.push(Error::new(at, message));
            }
        }
        errors
    }
}
