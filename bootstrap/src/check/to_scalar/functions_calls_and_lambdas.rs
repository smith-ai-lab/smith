//! Lowers functions, calls, and lambdas: a function and its name, `return`
//! and what leaves a function or a lambda early, and a call, which is lowered
//! in place, with its arguments.

use super::*;

/// Whether a `return` is written in a block, and not in a lambda in it,
/// which leaves the lambda.
pub(super) fn has_return(body: &[Stmt]) -> bool {
    stops(body, false, true)
}

/// Whether the block of a `loop` can answer `.stop`: it has a `break` that
/// is not in a loop of its own, or a `return` that is not in a lambda.
pub(super) fn can_stop(body: &[Stmt]) -> bool {
    stops(body, true, true)
}

/// Whether a block holds a `break` that ends the loop it is the block of, if
/// `breaks` asks for one, or a `return` that leaves the function around it,
/// if `returns` asks for one.
pub(super) fn stops(body: &[Stmt], breaks: bool, returns: bool) -> bool {
    fn one(e: &Expr, breaks: bool, returns: bool) -> bool {
        let any = |es: &mut dyn Iterator<Item = &Expr>, b: bool, r: bool| {
            let mut found = false;
            for e in es {
                found = found || one(e, b, r);
            }
            found
        };
        match e {
            Expr::Break { .. } => breaks,
            Expr::Return { .. } => returns,
            Expr::Call(call) => any(&mut call.args.iter().map(|a| &a.value), breaks, returns),
            Expr::Field { of, .. } => one(of, breaks, returns),
            Expr::Arith { left, right, .. } => {
                one(left, breaks, returns) || one(right, breaks, returns)
            }
            Expr::Case { payload, .. } => payload.as_ref().is_some_and(|p| one(p, breaks, returns)),
            Expr::Match(m) => {
                one(&m.of, breaks, returns)
                    || m.arms
                        .iter()
                        .any(|a| any(&mut values(&a.body), breaks, returns))
            }
            Expr::Tuple { items, .. } | Expr::List { items, .. } => {
                any(&mut items.iter(), breaks, returns)
            }
            Expr::Index { of, index, .. } => {
                one(of, breaks, returns) || one(index, breaks, returns)
            }
            Expr::Slice { of, from, to, .. } => {
                one(of, breaks, returns)
                    || [from, to]
                        .into_iter()
                        .flatten()
                        .any(|end| one(end, breaks, returns))
            }
            // A `return` in a lambda leaves the lambda.
            Expr::Lambda(l) => any(&mut values(&l.body), false, false),
            // A `break` in a loop ends that loop.
            Expr::Loop(l) => {
                let heads = (l.range.iter().flat_map(|r| [&r.from, &r.to]))
                    .chain(l.over.iter().map(|o| &o.value))
                    .chain(l.decreases.iter());
                heads.into_iter().any(|e| one(e, breaks, returns))
                    || any(&mut values(&l.body), false, returns)
            }
            Expr::Interp { parts, .. } => parts
                .iter()
                .any(|p| matches!(p, Part::Expr(e) if one(e, breaks, returns))),
            Expr::Text { .. } | Expr::Number { .. } | Expr::Name { .. } | Expr::Unbuilt { .. } => {
                false
            }
        }
    }
    values(body).any(|e| one(e, breaks, returns))
}

impl<'a> Lowering<'a> {
    pub(super) fn function(&mut self, instance: &Instance) -> form::Function {
        let defs = self.typed.defs;
        if let Template::Given(k) = instance.template {
            let name = self.given_name(k);
            let ty = defs.text(&instance.args[0], &Scope::default());
            return self
                .given(k, &instance.args)
                .named(spaceless(format!("<{name}[{ty}]>")));
        }
        let view = defs.view(instance.template);
        let name = self.name_of(instance);
        let mut out = Out::new();
        out.written = form::names::intern(&name);
        out.place = form::Place::of(&name);
        for (name, ty) in &view.params {
            let ty = ty.subst(&instance.args);
            let types = self.scalars(&ty);
            let values = out.params(&types);
            out.define(name, ty, values);
        }
        let gives = view.gives.subst(&instance.args);
        let code = Code::Stmts(&view.f.body);
        let mut result = self.leaving(&mut out, code, &instance.args, (&gives, true), Vec::new());
        if view.gives == Ty::Nothing {
            result.clear();
        }
        let results = self.scalars(&gives);
        out.blocks[out.current].end = form::End::Return(result);
        form::Function {
            types: out.types,
            results,
            blocks: out.blocks,
            cold: false,
            name,
        }
    }

    /// What the map of an executable calls the copy of a function: its module
    /// and its name as written, and the types it is made for in brackets.
    pub(super) fn name_of(&self, instance: &Instance) -> String {
        let defs = self.typed.defs;
        // The types it is made for: those the implementation is for, and then
        // the function's own.
        let (pos, name, types) = match instance.template {
            Template::Free(f) => (
                defs.file.functions[f].pos,
                &defs.file.functions[f].name,
                instance.args.clone(),
            ),
            Template::Impl(i, fi) => {
                let info = defs.impls[i]
                    .as_ref()
                    .expect("an implementation that is known");
                let f = &defs.file.impls[i].functions[info.fns[fi].expect("implemented")];
                let own = instance.args.iter().skip(info.scope.names.len());
                let types = (info.args.iter())
                    .map(|ty| ty.subst(&instance.args))
                    .chain(own.cloned())
                    .collect();
                (f.pos, &f.name, types)
            }
            Template::Given(_) => unreachable!("a given implementation is named by its kind"),
        };
        let module = if of_prelude(pos) {
            "prelude"
        } else {
            let number = module_of(pos);
            (defs.file.modules.iter())
                .find(|(n, _)| *n == number)
                .map_or("main", |(_, label)| label.as_str())
        };
        let mut text = format!("{module}.{}", written_name(name));
        if !types.is_empty() {
            let args: Vec<String> = (types.iter())
                .map(|ty| defs.text(ty, &Scope::default()))
                .collect();
            text.push_str(&format!("[{}]", args.join(",")));
        }
        spaceless(text)
    }

    /// Lowers the body of a function or of a lambda, or the block of a `loop`
    /// over a value, which gives `gives`, and gives the values it gives. A
    /// `return` in it ends its block with them when `whole` says the body is
    /// the whole of a function, and otherwise jumps to the block where what
    /// the body gives is known, which also knows what the names that
    /// outlive the body, `limits` of each frame, are then.
    pub(super) fn leaving(
        &mut self,
        out: &mut Out<'a>,
        code: Code<'a>,
        env: &[Ty],
        (gives, whole): (&Ty, bool),
        limits: Vec<usize>,
    ) -> Vec<form::Value> {
        let kept = match code {
            Code::Each(l, _) => self.kept_of(out, l),
            Code::Stmts(_) => None,
        };
        out.returns.push(Returns {
            gives: gives.clone(),
            limits: limits.clone(),
            ends: Vec::new(),
            kept,
            of: match code {
                Code::Each(l, _) => Some(addr(l)),
                Code::Stmts(_) => None,
            },
        });
        let result = match code {
            Code::Stmts(body) => self.block(out, body, env),
            Code::Each(l, read) => {
                out.loops.push(Round {
                    carried: Vec::new(),
                    leaving: Vec::new(),
                    body: true,
                });
                self.block(out, &l.body, env);
                out.loops.pop();
                if read {
                    vec![self.flow(out, false)]
                } else {
                    Vec::new()
                }
            }
        };
        let mut ends = out.returns.pop().expect("pushed").ends;
        if ends.is_empty() {
            return result;
        }
        let types = self.scalars(gives);
        for end in &mut ends {
            end.args.truncate(types.len());
        }
        let reached = !out.dead.contains(&out.current);
        if whole {
            // What follows the last `return` ends with the values it gave, as
            // it is reached by nothing and the end of a function takes some.
            let last = ends.last().map(|end| end.args.clone());
            for end in ends {
                out.blocks[end.block].end = form::End::Return(end.args);
            }
            return if reached {
                result
            } else {
                last.unwrap_or_default()
            };
        }
        // What follows the last `return` is reached by nothing, and jumps with
        // the values the `return` gave, as a jump has to give some.
        let (args, names) = match ends.last() {
            Some(last) if !reached => (last.args.clone(), last.names.clone()),
            _ => (result, self.names_now(out, &limits)),
        };
        ends.push(Leaving {
            block: out.current,
            args,
            names,
        });
        self.join_returns(out, &types, ends, &limits)
    }

    /// Ends the block being filled by leaving the function or the lambda
    /// being lowered with `values`. What follows is reached by nothing.
    pub(super) fn leave(&mut self, out: &mut Out<'a>, values: Vec<form::Value>) {
        let limits = out.returns.last().expect("checked").limits.clone();
        let names = self.names_now(out, &limits);
        let block = out.current;
        let returns = out.returns.last_mut().expect("checked");
        returns.ends.push(Leaving {
            block,
            args: values,
            names,
        });
        out.current = out.new_block();
        out.dead.push(out.current);
    }

    /// Makes a block where the blocks of `ends`, which give it values, meet,
    /// and gives the values it takes. The names that outlive the body and
    /// are not the same at every end are taken too, and have their values
    /// there from then on.
    pub(super) fn join_returns(
        &mut self,
        out: &mut Out<'a>,
        types: &[form::Type],
        ends: Vec<Leaving>,
        limits: &[usize],
    ) -> Vec<form::Value> {
        let mut changed = Vec::new();
        for (f, &n) in limits.iter().enumerate() {
            for at in 0..n {
                if ends.iter().any(|e| e.names[f][at] != ends[0].names[f][at]) {
                    changed.push((f, at));
                }
            }
        }
        let mut all = types.to_vec();
        for &(f, at) in &changed {
            all.extend(ends[0].names[f][at].iter().map(|v| out.types[v.0]));
        }
        let sizes: Vec<usize> = (changed.iter())
            .map(|&(f, at)| ends[0].names[f][at].len())
            .collect();
        let ends = (ends.into_iter())
            .map(|mut end| {
                for &(f, at) in &changed {
                    end.args.extend(end.names[f][at].clone());
                }
                (end.block, end.args)
            })
            .collect();
        let params = out.join(&all, ends);
        let mut at_param = types.len();
        for ((f, at), size) in changed.into_iter().zip(sizes) {
            out.frames[f][at].values = params[at_param..at_param + size].to_vec();
            at_param += size;
        }
        params[..types.len()].to_vec()
    }

    /// An argument for a parameter, as the name the parameter is then.
    pub(super) fn argument(
        &mut self,
        out: &mut Out<'a>,
        value: &'a Expr,
        (name, ty): &(String, Ty),
        env: &[Ty],
    ) -> Bound<'a> {
        let (values, func) = if let Ty::Fn(_) = ty {
            let func = match value {
                Expr::Lambda(l) => Func::Lambda(l, out.at, env.to_vec()),
                Expr::Name { name, .. } => match out.find(name) {
                    Some(at) => out.names()[at].func.clone().expect("a function"),
                    None => Func::Named(Instance {
                        template: Template::Free(self.typed.fn_values[&addr(value)]),
                        args: Vec::new(),
                    }),
                },
                _ => unreachable!("checked"),
            };
            (Vec::new(), Some(func))
        } else {
            (self.passed(out, value, ty, env), None)
        };
        Bound {
            name: name.clone(),
            ty: ty.clone(),
            values,
            func,
        }
    }

    /// Lowers a call of a name that holds a function. A lambda's body is
    /// lowered in place, in the frame it is written in, with its parameters
    /// standing for the arguments.
    pub(super) fn call_value(
        &mut self,
        out: &mut Out<'a>,
        call: &'a Call,
        env: &[Ty],
    ) -> (Ty, Vec<form::Value>) {
        self.call_value_as(out, call, env, false)
    }

    /// `call_value`, where `quiet` says that nothing reads what the block of
    /// a `loop` gives, which cannot be `.stop`.
    pub(super) fn call_value_as(
        &mut self,
        out: &mut Out<'a>,
        call: &'a Call,
        env: &[Ty],
        quiet: bool,
    ) -> (Ty, Vec<form::Value>) {
        let at = out.find(&call.callee).expect("checked");
        let local = &out.names()[at];
        let func = local.func.clone().expect("a function");
        let Ty::Fn(items) = local.ty.clone() else {
            unreachable!("checked")
        };
        let (gives, params) = items.split_last().expect("a result");
        let mut args: Vec<Option<Bound<'a>>> = vec![None; params.len()];
        let mut next = 0;
        for arg in &call.args {
            let at = if arg.after {
                params.len() - 1
            } else {
                next += 1;
                next - 1
            };
            let param = (String::new(), params[at].clone());
            args[at] = Some(self.argument(out, &arg.value, &param, env));
        }
        let args = args.into_iter().map(|a| a.expect("given"));
        let mut result = match func {
            Func::Named(instance) => {
                let function = self.place(Body::Copy(instance));
                let args = args.flat_map(|a| a.values).collect();
                let types = self.scalars(gives);
                out.emit(form::Op::Call { function, args }, types)
            }
            Func::Lambda(lambda, frame, lambda_env) => {
                let caller = std::mem::replace(&mut out.at, frame);
                let before = out.names().len();
                let mut limits: Vec<usize> = out.frames.iter().map(Vec::len).collect();
                limits[frame] = before;
                for ((name, _), mut arg) in lambda.params.iter().zip(args) {
                    arg.name = name.clone();
                    out.names_mut().push(arg);
                }
                let code = Code::Stmts(&lambda.body);
                let result = self.leaving(out, code, &lambda_env, (gives, false), limits);
                out.names_mut().truncate(before);
                out.at = caller;
                result
            }
            Func::Each(each, frame, each_env) => {
                let caller = std::mem::replace(&mut out.at, frame);
                let before = out.names().len();
                let mut limits: Vec<usize> = out.frames.iter().map(Vec::len).collect();
                limits[frame] = before;
                let over = each.over.as_ref().expect("a loop over a value");
                for mut arg in args {
                    arg.name = over.name.clone();
                    out.names_mut().push(arg);
                }
                let code = Code::Each(each, !quiet);
                let result = self.leaving(out, code, &each_env, (gives, false), limits);
                out.names_mut().truncate(before);
                out.at = caller;
                result
            }
        };
        if *gives == Ty::Nothing {
            result.clear();
        }
        (gives.clone(), result)
    }
}
