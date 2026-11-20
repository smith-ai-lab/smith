//! Checks functions, calls, and lambdas: a function's signature, parameters,
//! and defaults, what it returns, a call and its arguments, a lambda, a
//! function used as a value, and calls that close a cycle.

use super::*;

impl<'a> Checker<'a> {
    /// At a call of a function from itself that is the last thing it does,
    /// `arith` must prove that the measure of its `decreases`, worked out from
    /// the arguments, is not below zero and is smaller than the measure now.
    /// A call that is not the last thing the function does is refused, with
    /// every call of a function that has no `decreases`, where `recursion`
    /// looks for them, and is not looked at here.
    fn call_decreases(&mut self, body: &mut Body<'a>, call: &Call, callee: usize, sig: &Signature) {
        let defs = self.defs;
        let Some(own) = body.f else { return };
        if !std::ptr::eq(own, &*defs.file.functions[callee])
            || own.decreases.is_empty()
            || body.lambdas > 0
            || body.contract
            || matches!(body.numbers, Numbers::Finding(_))
            || !tail_positions(own).contains(&call.pos)
        {
            return;
        }
        // A call with an argument for no parameter is reported, and proves nothing.
        let Some(places) = params_of(&sig.params, &call.args) else {
            return;
        };
        let mut given: Vec<Option<&Expr>> = vec![None; own.params.len()];
        for (arg, place) in call.args.iter().zip(places) {
            given[place] = Some(&arg.value);
        }
        // What a parameter is at the call is its argument, or its default when
        // the call leaves it out.
        let args: Vec<(&str, &Expr)> = (own.params.iter().zip(&given))
            .filter_map(|(p, g)| g.or(p.default.as_ref()).map(|e| (p.name.as_str(), e)))
            .collect();
        let outcome = match (
            self.measure_of(body, &own.decreases, &[]),
            self.measure_of(body, &own.decreases, &args),
        ) {
            (Some(before), Some(after)) => self.decreased_by(body, &own.decreases, &before, &after),
            _ => Err(proof::Unproven::Smaller(
                own.decreases
                    .iter()
                    .map(written_expr)
                    .collect::<Vec<_>>()
                    .join(", "),
            )),
        };
        let goal = match outcome {
            Ok(()) => return,
            Err(proof::Unproven::Smaller(m)) => format!("`{m}` is smaller at this call"),
            Err(proof::Unproven::NotBelowZero(m)) => {
                format!("`{m}` is not below zero at this call")
            }
        };
        self.errors.push(
            Error::new(
                call.pos,
                format!("cannot prove that {goal}, so `{}` may not end", own.name),
            )
            .with_fix(
                "check what the call needs first with an `if`, or make each call make the \
                 measure smaller",
            ),
        );
    }

    /// Checks the parts of the `decreases` of a function: each is a whole number
    /// of an unsigned type over the parameters, which is never computed when
    /// the program runs.
    pub(super) fn measure_parts(&mut self, body: &mut Body<'a>, f: &Function) {
        for e in &f.decreases {
            // It is an exact number, never computed, so what it does with a
            // number that does not fit is nothing the program asks of `arith`.
            let contract = std::mem::replace(&mut body.contract, true);
            let gives = self.expr(body, e, &Want::Any);
            body.contract = contract;
            // A number that is written is of the type the other parts are, as
            // the `2` of `decreases toks.size - at, 2` is.
            let written = matches!(
                e,
                Expr::Number {
                    negative: false,
                    ..
                }
            );
            if !written
                && !matches!(gives, Ty::Int(t) if !t.signed())
                && !matches!(gives, Ty::Unknown | Ty::Flex(_))
            {
                let given = self.defs.describe(&gives, body.scope);
                self.errors.push(Error::new(
                    e.pos(),
                    format!(
                        "`decreases` is a whole number of an unsigned type, and this is {given}"
                    ),
                ));
            }
        }
    }

    /// Checks parameters, each named once in `f`, and the types of the
    /// parameters and the result, among the type parameters of `scope`.
    pub(super) fn params(&mut self, f: &Function, scope: &Scope) {
        for (i, p) in f.params.iter().enumerate() {
            if f.params[..i].iter().any(|q| q.name == p.name) {
                self.errors.push(Error::new(
                    p.pos,
                    format!("`{}` is already defined in `{}`", p.name, f.name),
                ));
            }
            self.param_ty(&p.ty, scope);
        }
        if let Some(t) = &f.result {
            self.ty(t, scope);
        }
    }

    /// Checks the signature of function `f`: its type parameters, each
    /// named by a parameter's type, its types, and its list of effects.
    pub(super) fn signature(&mut self, f: usize) {
        let defs = self.defs;
        let def = &defs.file.functions[f];
        if defs.function_names.get("main") != Some(&f) {
            self.generics(&def.generics, &format!("`{}`", def.name), Owner::Function);
            for g in &def.generics {
                let named = (def.params.iter())
                    .any(|p| type_names(&p.ty).iter().any(|(n, _)| *n == g.name))
                    || defs.named_by_out(def, &g.name);
                if !named {
                    self.errors.push(Error::unsupported(
                        g.pos,
                        "a type parameter that no parameter's type names",
                    ));
                }
            }
        }
        defs.requirements(&def.needs, &defs.signatures[f].scope, &mut self.errors);
        self.params(def, &defs.signatures[f].scope);
        self.defaults(def, &defs.signatures[f]);
        self.contracts(def, &defs.signatures[f]);
        self.effects(&def.effects);
        self.parameter_effects(def);
    }

    /// Checks a default: it uses no name, carries no effect, and gives
    /// `needed`, among the type parameters of `scope`. `params` are the
    /// parameters of the function it is in, if it is in one.
    pub(super) fn default(
        &mut self,
        value: &Expr,
        needed: &Ty,
        scope: &'a Scope,
        params: Vec<&'a str>,
        extra: Vec<String>,
    ) {
        let mut body = Body {
            f: None,
            default: Some(params),
            scope,
            listed: Vec::new(),
            extra,
            unknown_effect: false,
            missing: Vec::new(),
            names: Vec::new(),
            params: 0,
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
        let gives = self.expr(&mut body, value, &Want::Ty(needed.clone()));
        self.expect(&mut body, value, &gives, needed);
    }

    /// Checks the defaults of the parameters of `f`, whose signature is
    /// `sig`: the parameters with one come after those without, and each
    /// default is as `default` requires.
    pub(super) fn defaults(&mut self, f: &'a Function, sig: &'a Signature) {
        let names: Vec<&str> = f.params.iter().map(|p| p.name.as_str()).collect();
        let mut first = None;
        for (i, (p, (_, ty))) in f.params.iter().zip(&sig.params).enumerate() {
            // A final parameter that takes a function may come after those
            // with defaults, as a lambda after a call is for it.
            let last_fn = i + 1 == f.params.len() && p.ty.function;
            match (&p.default, first) {
                (None, Some(_)) if last_fn => {}
                (Some(value), _) => {
                    first = first.or(Some(p.name.as_str()));
                    let extra = sig.fn_effects.get(i).cloned().unwrap_or_default();
                    self.default(value, ty, &sig.scope, names.clone(), extra);
                }
                (None, Some(q)) => self.errors.push(
                    Error::new(
                        p.pos,
                        format!(
                            "`{}` has no default, and comes after `{q}`, which has one",
                            p.name
                        ),
                    )
                    .with_fix(format!(
                        "move `{}` before `{q}`, or give it a default",
                        p.name
                    )),
                ),
                (None, None) => {}
            }
        }
    }

    /// Reports a body that ends with a statement that gives nothing, the
    /// one at `pos`, where `f` returns the type written `t`.
    pub(super) fn ends_with_nothing(&mut self, f: &Function, pos: Pos, t: &str) {
        self.errors.push(Error::new(
            pos,
            format!(
                "`{}` returns `{t}`, and its body ends with a binding or an assignment, \
                 which gives nothing",
                f.name
            ),
        ));
    }

    /// Checks a `return`: what it gives is the result of the function or the
    /// lambda around it, and a `return` with no value is where that gives
    /// nothing.
    pub(super) fn returning(&mut self, body: &mut Body<'a>, value: Option<&Expr>, pos: Pos) {
        let defs = self.defs;
        let needed = body.returns.clone();
        match value {
            None => {
                if needed != Ty::Nothing && !needed.unknown() {
                    let needed = defs.describe(&needed, body.scope);
                    self.errors.push(
                        Error::new(
                            pos,
                            format!("this `return` has no value, where {needed} is needed"),
                        )
                        .with_fix("write the value after `return`"),
                    );
                }
            }
            Some(value) => {
                let want = match &needed {
                    Ty::Nothing => Want::Unused,
                    t if t.unknown() => Want::Any,
                    t => Want::Ty(t.clone()),
                };
                let gives = self.expr(body, value, &want);
                match (&needed, &body.returned) {
                    (Ty::Nothing, _) => {
                        if !matches!(gives, Ty::Nothing | Ty::Unknown) {
                            let given = defs.describe(&gives, body.scope);
                            self.errors.push(Error::new(
                                value.pos(),
                                format!("this `return` gives {given}, where nothing is needed"),
                            ));
                        }
                    }
                    (t, Some(first)) if t.unknown() => {
                        let first = first.clone();
                        self.expect(body, value, &gives, &first);
                    }
                    (t, None) if t.unknown() => body.returned = Some(gives),
                    (t, _) => self.expect(body, value, &gives, t),
                }
                if body.lambdas == 0 && !body.tails.is_empty() {
                    self.ensure(body, value);
                }
            }
        }
    }

    /// Settles the types of lambda `l` among those `wanted` of it, the
    /// result last, which are unknown where what it is passed to leaves them
    /// open: a type it writes takes the place of one that is open, and one
    /// that is not the type wanted is an error at the type.
    pub(super) fn written_types(&mut self, body: &mut Body<'a>, l: &Lambda, wanted: &mut [Ty]) {
        let defs = self.defs;
        let written = l.types.iter().chain(std::iter::once(&l.result));
        for (slot, t) in wanted.iter_mut().zip(written) {
            let Some(t) = t else { continue };
            let ty = self.ty(t, body.scope);
            if ty == Ty::Unknown {
                continue;
            }
            if agrees(slot, &ty) {
                *slot = ty;
            } else {
                self.errors.push(Error::new(
                    t.pos,
                    format!(
                        "this is {}, where the function type needs {}",
                        defs.describe(&ty, body.scope),
                        defs.describe(slot, body.scope)
                    ),
                ));
            }
        }
    }

    /// Checks a lambda passed where a function is needed whose parameters
    /// and result, the result last, are of the types `wanted`, which may be
    /// partly known, and gives its type. Its body is checked where it is
    /// written, against the effects of the function it is written in, and
    /// it may assign that function's `var`s.
    pub(super) fn lambda(
        &mut self,
        body: &mut Body<'a>,
        l: &Lambda,
        wanted: &[Ty],
        quiet: bool,
    ) -> Ty {
        let defs = self.defs;
        let params = wanted.len() - 1;
        let mut wanted = wanted.to_vec();
        if l.params.len() == params {
            self.written_types(body, l, &mut wanted);
        }
        let wanted = &wanted[..];
        let (result, params) = wanted.split_last().expect("a result");
        if l.params.len() != params.len() {
            let needed = Ty::Fn(wanted.to_vec());
            self.errors.push(Error::new(
                l.pos,
                format!(
                    "this lambda takes {}, where {} is needed, which takes {}",
                    count(l.params.len(), "parameter"),
                    defs.describe(&needed, body.scope),
                    params.len()
                ),
            ));
            return Ty::Unknown;
        }
        let outer = body.names.len();
        let vars = body.vars.len();
        let known = body.facts.mark();
        let assigned = self.assigned_vars(body, &[&l.body]);
        self.forget_all(body);
        let loops = body.loops;
        body.outer_loops += std::mem::take(&mut body.loops);
        let mut types = Vec::new();
        for (i, ((name, pos), ty)) in l.params.iter().zip(params).enumerate() {
            let twice = l.params[..i].iter().any(|(n, _)| n == name)
                || body.names.iter().any(|(n, _)| n == name);
            if twice {
                self.errors.push(self.defined_twice(body, name, *pos));
            }
            types.push(ty.clone());
            // The ownership check reads the type of a parameter from here.
            self.types.insert(addr(&l.params[i]), ty.clone());
            body.names.push((name.clone(), ty.clone()));
        }
        let want = match result {
            Ty::Nothing => Want::Unused,
            Ty::Unknown if quiet => Want::Ty(Ty::Unknown),
            t if t.unknown() => Want::Any,
            t => Want::Ty(t.clone()),
        };
        let outer_returns = std::mem::replace(&mut body.returns, result.clone());
        let outer_returned = body.returned.take();
        body.lambdas += 1;
        let end = self.block(body, &l.body, &want);
        body.lambdas -= 1;
        body.returns = outer_returns;
        let returned = std::mem::replace(&mut body.returned, outer_returned);
        let result = match returned {
            Some(t) if result.unknown() => &t.clone(),
            _ => result,
        };
        let gives = match (end, result) {
            _ if proof::ends_in_exit(&l.body) => result.clone(),
            (End::Value(gives, value), Ty::Nothing) => {
                if !matches!(gives, Ty::Nothing | Ty::Unknown) {
                    self.expect(body, value, &gives, result);
                }
                Ty::Nothing
            }
            (_, Ty::Nothing) => Ty::Nothing,
            (End::Value(gives, value), result) if !result.unknown() => {
                self.expect(body, value, &gives, result);
                result.clone()
            }
            (End::Value(gives, _), _) => gives,
            (End::Empty, result) if !result.unknown() => {
                let needed = defs.describe(result, body.scope);
                self.errors.push(Error::new(
                    l.end,
                    format!(
                        "this lambda's body is empty, which gives nothing, where {needed} is needed"
                    ),
                ));
                Ty::Unknown
            }
            (End::Binding(pos), result) if !result.unknown() => {
                let needed = defs.describe(result, body.scope);
                self.errors.push(Error::new(
                    pos,
                    format!(
                        "this lambda ends with a binding or an assignment, which gives nothing, \
                         where {needed} is needed"
                    ),
                ));
                Ty::Unknown
            }
            (End::Empty | End::Binding(_), _) => Ty::Nothing,
        };
        body.names.truncate(outer);
        body.vars.truncate(vars);
        body.facts.reset(&known);
        self.forget(body, &assigned);
        body.outer_loops -= loops;
        body.loops = loops;
        types.push(gives);
        Ty::func(types)
    }

    /// Checks the name of function `f`, at `pos`, passed as a value, and
    /// gives its type. A function type states no effect, so the effects of
    /// `f` are required of the function the name is written in, as those
    /// of a lambda that calls it are.
    pub(super) fn function_value(
        &mut self,
        body: &mut Body<'a>,
        expr: &Expr,
        f: usize,
        pos: Pos,
    ) -> Ty {
        let defs = self.defs;
        let sig = &defs.signatures[f];
        let name = &defs.file.functions[f].name;
        let error = if !sig.scope.names.is_empty() {
            Error::unsupported(pos, "passing a function with type parameters as a value")
        } else if sig.params.iter().any(|(_, t)| t.holds_fn()) {
            Error::unsupported(pos, "passing a function that takes a function as a value")
        } else if !defs.file.functions[f].requires.is_empty() {
            Error::new(
                pos,
                format!(
                    "`{name}` has a `requires`, which a function type does not state, so it is \
                     only called by its name"
                ),
            )
        } else {
            for effect in &sig.effects {
                self.needs(body, effect, pos, &format!("`{name}`"));
            }
            self.fn_values.insert(addr(expr), f);
            let mut items: Vec<Ty> = sig.params.iter().map(|(_, t)| t.clone()).collect();
            items.push(sig.gives.clone());
            return Ty::func(items);
        };
        self.errors.push(error);
        Ty::Unknown
    }

    /// Checks a call of a name that holds a function whose parameters and
    /// result, the result last, are of the types `items`. Its arguments are
    /// given by position, and it carries no effect.
    pub(super) fn value_call(&mut self, body: &mut Body<'a>, call: &Call, items: &[Ty]) -> Ty {
        self.value_calls.insert(addr(call));
        let (gives, params) = items.split_last().expect("a result");
        let name = &call.callee;
        let mut given = vec![false; params.len()];
        let mut next = 0;
        for arg in &call.args {
            let at = if arg.after {
                params.len().checked_sub(1)
            } else {
                next += 1;
                Some(next - 1).filter(|i| *i < params.len())
            };
            let error = if let Some((_, pos)) = &arg.name {
                Some(Error::new(
                    *pos,
                    format!(
                        "the arguments of `{name}` are given by position, as it is a function a name holds"
                    ),
                ))
            } else {
                match at {
                    Some(i) if given[i] => Some(Error::new(
                        arg.value.pos(),
                        format!("the argument for the last parameter of `{name}` is given twice"),
                    )),
                    Some(i) if arg.after && !matches!(params[i], Ty::Fn(_) | Ty::Unknown) => {
                        Some(Error::new(
                            arg.value.pos(),
                            format!(
                                "a lambda after a call is for its last parameter, and that of \
                                 `{name}` takes no function"
                            ),
                        ))
                    }
                    Some(_) => None,
                    None => Some(Error::new(
                        arg.value.pos(),
                        format!(
                            "`{name}` takes {}, and this is one more",
                            count(params.len(), "argument")
                        ),
                    )),
                }
            };
            if let Some(error) = error {
                self.errors.push(error);
                self.expr(body, &arg.value, &Want::Ty(Ty::Unknown));
                continue;
            }
            let i = at.expect("a parameter");
            given[i] = true;
            let value = self.expr(body, &arg.value, &Want::Ty(params[i].clone()));
            self.expect(body, &arg.value, &value, &params[i]);
        }
        let missing = given.iter().filter(|g| !**g).count();
        if missing > 0 {
            self.errors.push(Error::new(
                call.end,
                format!(
                    "`{name}` takes {}, and this call gives {}",
                    count(params.len(), "argument"),
                    params.len() - missing
                ),
            ));
        }
        gives.clone()
    }

    /// Reports a type given for a type parameter that holds a function,
    /// and gives whether there is one.
    pub(super) fn function_for_parameter(&mut self, sol: &[Option<Ty>], pos: Pos) -> bool {
        if sol.iter().flatten().any(Ty::holds_fn) {
            self.errors.push(Error::unsupported(
                pos,
                "a function given for a type parameter",
            ));
            return true;
        }
        false
    }

    /// Checks the arguments of a call that names nothing there is, for the
    /// mistakes in them.
    pub(super) fn unknown_call(&mut self, body: &mut Body<'a>, call: &Call) -> Ty {
        for arg in &call.args {
            self.expr(body, &arg.value, &Want::Ty(Ty::Unknown));
        }
        Ty::Unknown
    }

    pub(super) fn call(&mut self, body: &mut Body<'a>, call: &Call, want: &Want) -> Ty {
        let defs = self.defs;
        let name = call.callee.as_str();
        if let Some((q, at)) = &call.qualifier {
            if let Some(e) = defs.enum_at(*at, q)
                && !body.names.iter().any(|(n, _)| n == q)
            {
                return self.qualified_case(body, call, e, want);
            }
            let Some(k) = defs.trait_at(*at, q) else {
                self.errors.push(if body.names.iter().any(|(n, _)| n == q) {
                    Error::unsupported(call.name_pos, "calling what a field gives")
                } else {
                    Error::new(*at, format!("there is no trait named `{q}`"))
                        .with_fix("to write a name after a module, import the module with `use`")
                });
                return self.unknown_call(body, call);
            };
            let t = &defs.file.traits[k];
            let Some(fi) = traits::trait_function(t, name) else {
                self.errors.push(Error::new(
                    call.name_pos,
                    format!("`{q}` has no function named `{name}`"),
                ));
                return self.unknown_call(body, call);
            };
            return self.trait_call(body, call, k, fi, want);
        }
        if let Some((_, Ty::Fn(items))) = body.names.iter().rfind(|(n, _)| n == name) {
            let items = items.clone();
            return self.value_call(body, call, &items);
        }
        if let Some(s) = defs.struct_at(call.name_pos, name) {
            return self.construction(body, call, s, want);
        }
        if is_prelude_call(call, "__eprint") {
            let print = Signature::print();
            self.arguments(body, call, &print, true, None, None);
            return Ty::Nothing;
        }
        if is_prelude_call(call, "__end") {
            let end = Signature::end();
            self.arguments(body, call, &end, true, None, None);
            return Ty::Nothing;
        }
        if is_prelude_call(call, "__convert") {
            return self.convert_intrinsic(body, call, want);
        }
        if is_intrinsic(call) {
            return self.intrinsic(body, call);
        }
        if is_push(defs, call) {
            return self.push(body, call, want);
        }
        if is_freeze(defs, call) {
            return self.freeze(body, call, want);
        }
        if is_utf8(defs, call) {
            return self.utf8(body, call);
        }
        if let Some(narrowing) = narrowing(defs, call) {
            return self.narrow(body, call, narrowing, want);
        }
        if let Some(provided) = defs.provided_by_name(name) {
            let print = (provided.signature)();
            if self.effect_in_default(body, call, &print) {
                return self.unknown_call(body, call);
            }
            self.arguments(body, call, &print, true, None, None);
            return Ty::Nothing;
        }
        if let (Some(effect), false) = (
            defs.effect_named(name),
            defs.function_names.contains_key(name),
        ) {
            return self.handler_call(body, call, effect);
        }
        // A name that holds the function `g` by the type `g` is called as `g` is.
        let named = body
            .names
            .iter()
            .rfind(|(n, _)| n == name)
            .and_then(|(_, t)| match t {
                Ty::Named(g) => Some(*g),
                _ => None,
            });
        if let Some(f) = named.or_else(|| defs.function(call.name_pos, name)) {
            let sig = &defs.signatures[f];
            if named.is_some() {
                self.named_calls.insert(addr(call), f);
                if call.args.len() < sig.params.len() {
                    self.errors.push(Error::unsupported(
                        call.pos,
                        "leaving out an argument of a function called by its type",
                    ));
                    return self.unknown_call(body, call);
                }
            }
            if self.effect_in_default(body, call, sig) {
                return self.unknown_call(body, call);
            }
            let refused = self.refusals;
            let refused_errors = self.errors.len();
            let placed = match want {
                Want::Ty(t) => Some(t),
                _ => None,
            };
            let mut sol = self.arguments(body, call, sig, false, None, placed);
            defs.complete_outs(&sig.scope.requires, &mut sol, body.scope);
            for (i, d) in sig.param_defaults.iter().enumerate() {
                if let (None, Some(d)) = (&sol[i], d) {
                    let ty = d.solved(&sol);
                    sol[i] = (!ty.unknown()).then_some(ty);
                }
            }
            if self.refusals != refused || self.function_for_parameter(&sol, call.pos) {
                return Ty::Unknown;
            }
            for r in &sig.scope.requires {
                let types: Vec<Ty> = r.types.iter().map(|t| t.solved(&sol)).collect();
                if types.contains(&Ty::Unknown) {
                    continue;
                }
                match defs.meets_types(r.of, &types, body.scope, &mut Vec::new()) {
                    Found::No => {
                        let because = match types.as_slice() {
                            [t] => defs.because(r.of, t, body.scope),
                            _ => String::new(),
                        };
                        self.errors.push(Error::new(
                            call.pos,
                            format!(
                                "there is no implementation of `{}` for {}, which `{name}` \
                                 needs{because}",
                                defs.file.traits[r.of].name,
                                defs.types_of(r.of, &types, body.scope)
                            ),
                        ));
                    }
                    Found::Two(a, b) => self.errors.push(defs.ambiguous(call.pos, a, b)),
                    Found::Yes(..) | Found::Unknown => {}
                }
            }
            self.contract_call(body, call, &defs.file.functions[f], sig);
            self.call_decreases(body, call, f, sig);
            // A division that wraps is of a divisor that is not zero.
            if matches!(name, "wrapping_div" | "wrapping_rem")
                && of_std(defs.file.functions[f].pos)
                && self.errors.len() == refused_errors
            {
                self.wrapping_division(body, call, sig);
            }
            return sig.gives.solved(&sol);
        }
        let mut of = (defs.trait_fns_at(call.name_pos, name))
            .cloned()
            .unwrap_or_default();
        if let [(k, _)] = of[..] {
            of.extend(defs.rivals(k, name, call.pos));
        }
        match of.as_slice() {
            [(k, fi)] => self.trait_call(body, call, *k, *fi, want),
            of if !of.is_empty() => {
                let traits: Vec<&str> = (of.iter())
                    .map(|(k, _)| written_name(&defs.file.traits[*k].name))
                    .collect();
                let fixes: Vec<String> = traits.iter().map(|t| format!("`{t}.{name}`")).collect();
                self.errors.push(
                    Error::new(
                        call.pos,
                        format!("`{name}` is a function of {}", names(&traits)),
                    )
                    .with_fix(format!("write {}", fixes.join(" or "))),
                );
                self.unknown_call(body, call)
            }
            _ => {
                self.errors.push(Error::new(
                    call.pos,
                    format!("there is no function named `{name}`"),
                ));
                self.unknown_call(body, call)
            }
        }
    }

    /// Before a lambda is checked against the function a trait's function
    /// takes, gives the types that are not yet known but which the
    /// implementation the known ones choose fixes: its `out` types, and the
    /// type the call gives when the call's value is what is wanted. Gives
    /// whether an implementation was not found, which is reported.
    pub(super) fn completed(
        &mut self,
        body: &Body,
        call: &Call,
        (k, want): (usize, &Want),
        sig: &Signature,
        sol: &mut [Option<Ty>],
    ) -> bool {
        let defs = self.defs;
        let outs = &defs.outs[k];
        // The type the call gives is that of what the lambda gives, so it
        // may be known only from what is wanted.
        let gives = match &sig.gives {
            Ty::Param(r) if !outs[*r] => Some(*r),
            _ => None,
        };
        if let (Some(r), Want::Ty(wanted)) = (gives, want)
            && sol[r].is_none()
            && !wanted.unknown()
        {
            sol[r] = Some(wanted.clone());
        }
        if (sol.iter().zip(outs).enumerate())
            .any(|(i, (t, out))| !out && t.is_none() && Some(i) != gives)
        {
            return false;
        }
        let types: Vec<Ty> = sol
            .iter()
            .map(|t| t.as_ref().map_or(Ty::Unknown, Ty::concrete))
            .collect();
        match defs.fitting(k, &types) {
            Some(all) => {
                for (t, a) in sol.iter_mut().zip(all) {
                    if a != Ty::Unknown {
                        t.get_or_insert(a);
                    }
                }
                false
            }
            None => {
                self.errors
                    .push(self.no_implementation(body, call, k, &types));
                true
            }
        }
    }

    /// Checks the effects and the arguments of a call of a function with
    /// this signature, and gives the type each of its type parameters
    /// stands for, where the arguments say.
    pub(super) fn arguments(
        &mut self,
        body: &mut Body<'a>,
        call: &Call,
        signature: &Signature,
        is_print: bool,
        via: Option<(usize, &Want)>,
        placed: Option<&Ty>,
    ) -> Vec<Option<Ty>> {
        let vars = &signature.scope;
        // What the place of the call's result says the type parameters are,
        // which only a literal that nothing else decides takes.
        let mut from_place: Vec<Option<Ty>> = vec![None; vars.names.len()];
        if let Some(t) = placed {
            seed(&signature.gives, t, &mut from_place);
        }
        let mut sol: Vec<Option<Ty>> = vec![None; vars.names.len()];
        let comparing = call.args.len() == 2
            && matches!(&call.qualifier, Some((q, _)) if q == "Ord" || q == "Eq")
            && matches!(call.callee.as_str(), "compare" | "eq");
        for effect in &signature.effects {
            self.needs(body, effect, call.pos, &format!("`{}`", call.callee));
        }
        let mut given: Vec<bool> = vec![false; signature.params.len()];
        // The whole number literals whose type is not known when they are
        // reached, which are checked after the other arguments, with the
        // place of the parameter each is for.
        let mut deferred: Vec<(usize, &Expr)> = Vec::new();
        let errors_before = self.errors.len();
        let mut lambdas: Vec<(usize, &Lambda)> = Vec::new();
        // An argument that needs the type the others say is checked after them,
        // so a call that waits would be checked after it.
        let needs_others = (call.args.iter()).any(|a| {
            matches!(
                a.value,
                Expr::Lambda(_) | Expr::Case { .. } | Expr::List { .. }
            )
        });
        let mut named = false;
        // Whether an argument named a parameter there is not. The mistake is
        // one, so a parameter written as a pattern, which no name can reach,
        // is not also reported missing.
        let mut unknown = false;
        let mut too_many = false;
        let mut next = 0;
        for arg in &call.args {
            // An argument `print` does not take yet is not looked into.
            let mut look_into = true;
            // The parameter the argument is for, if it is for one.
            let mut param = None;
            let callee = &call.callee;
            if arg.after {
                // A lambda after a call is for its last parameter.
                let last = signature.params.len().checked_sub(1);
                let error = match last {
                    None => Some(format!(
                        "`{callee}` takes no arguments, so no lambda goes after the call"
                    )),
                    Some(i) if !matches!(signature.params[i].1, Ty::Fn(_) | Ty::Unknown) => {
                        // The parameter is not then reported missing.
                        given[i] = true;
                        Some(format!(
                            "a lambda after a call is for its last parameter, and {}, the last \
                             parameter of `{callee}`, takes no function",
                            param_words(&signature.params[i].0, i)
                        ))
                    }
                    Some(i) if given[i] => Some(format!(
                        "the {} argument is given twice",
                        param_words(&signature.params[i].0, i)
                    )),
                    Some(i) => {
                        given[i] = true;
                        param = Some(i);
                        None
                    }
                };
                if let Some(message) = error {
                    self.errors.push(Error::new(arg.value.pos(), message));
                    self.refusals += 1;
                    continue;
                }
            } else {
                match &arg.name {
                    None if named => self.errors.push(
                        Error::new(
                            arg.value.pos(),
                            "an argument by position comes after a named one",
                        )
                        .with_fix("name it, or move it before the named arguments"),
                    ),
                    None if next < given.len() => {
                        given[next] = true;
                        param = Some(next);
                        next += 1;
                    }
                    None if is_print => {
                        look_into = false;
                        if !too_many {
                            self.errors.push(Error::unsupported(
                                arg.value.pos(),
                                "the `end` and `to` arguments of `print`",
                            ));
                        }
                        too_many = true;
                    }
                    None => {
                        if !too_many {
                            self.errors.push(Error::new(
                                arg.value.pos(),
                                format!(
                                    "`{}` takes {} arguments, and this is one more",
                                    call.callee,
                                    given.len()
                                ),
                            ));
                        }
                        too_many = true;
                    }
                    Some((name, pos)) => {
                        named = true;
                        match signature.params.iter().position(|(p, _)| p == name) {
                            Some(i) if given[i] => self.errors.push(Error::new(
                                *pos,
                                format!("the `{name}` argument is given twice"),
                            )),
                            Some(i) => {
                                given[i] = true;
                                param = Some(i);
                            }
                            None if is_print && (name == "end" || name == "to") => {
                                look_into = false;
                                self.errors.push(Error::unsupported(
                                    *pos,
                                    "the `end` and `to` arguments of `print`",
                                ));
                            }
                            None => {
                                unknown = true;
                                self.errors.push(Error::new(
                                    *pos,
                                    format!("`{}` has no parameter named `{name}`", call.callee),
                                ))
                            }
                        }
                    }
                }
            }
            if !look_into {
                continue;
            }
            if let (Some(i), Expr::Lambda(l)) = (param, &arg.value) {
                let pattern = &signature.params[i].1;
                if !matches!(pattern, Ty::Fn(_) | Ty::Unknown) {
                    let needed = self.defs.describe_pattern(pattern, &sol, vars, body.scope);
                    // A lambda has no name, so it has no `Function`.
                    let named = matches!(pattern, Ty::Param(p) if vars.requires.iter().any(
                        |r| r.of == self.defs.function && r.types.contains(&Ty::Param(*p))));
                    let message = if named {
                        "this is a lambda, and `Function` is given to a function with a name"
                            .to_string()
                    } else {
                        format!("this is a lambda, where {needed} is needed")
                    };
                    self.errors.push(Error::new(l.pos, message));
                    continue;
                }
            }
            if let (Some(via), Some(_), Expr::Lambda(_)) = (via, param, &arg.value)
                && !self.no_impl
            {
                self.no_impl = self.completed(body, call, via, signature, &mut sol);
            }
            // A lambda's parameters take the default of a type that nothing
            // before it settled.
            if let (Some(i), Expr::Lambda(_)) = (param, &arg.value) {
                for (p, d) in signature.param_defaults.iter().enumerate() {
                    if let (None, Some(d), true) = (&sol[p], d, signature.params[i].1.mentions(p)) {
                        let ty = d.solved(&sol);
                        sol[p] = (!ty.unknown()).then_some(ty);
                    }
                }
            }
            let quiet = self.no_impl || matches!(via, Some((_, Want::Ty(Ty::Unknown))));
            let wanted = match param {
                Some(_) if quiet => Want::Ty(Ty::Unknown),
                Some(i) => match signature.params[i].1.partial(&sol) {
                    // A type that is not known yet is not one a mistake
                    // already explains.
                    Ty::Unknown if signature.params[i].1 != Ty::Unknown => Want::Any,
                    t => Want::Ty(t),
                },
                None => Want::Ty(Ty::Unknown),
            };
            if let (Some(i), Want::Any) = (param, &wanted)
                && (matches!(arg.value, Expr::Number { .. })
                    || matches!(&arg.value, Expr::Match(m) if literal_blocks(m)))
            {
                deferred.push((i, &arg.value));
                continue;
            }
            // A call whose result a literal in it may decide, for a parameter
            // nothing says the type of yet, waits for the other arguments,
            // unless one of them needs what the earlier ones say.
            if let (Some(i), Expr::Call(_), Want::Any) = (param, &arg.value, &wanted)
                && !needs_others
            {
                deferred.push((i, &arg.value));
                continue;
            }
            // A list written out whose element type is not settled yet.
            if let (Some(i), Expr::List { .. }, Want::Ty(t)) = (param, &arg.value, &wanted)
                && *t != Ty::Unknown
                && t.unknown()
            {
                deferred.push((i, &arg.value));
                continue;
            }
            if let (Some(i), Expr::Lambda(l), Want::Ty(Ty::Fn(_))) = (param, &arg.value, &wanted) {
                lambdas.push((i, l));
            }
            let mark = body.extra.len();
            body.extra
                .extend(self.lambda_effects(signature, param, &arg.value));
            let gives = self.expr(body, &arg.value, &wanted);
            body.extra.truncate(mark);
            match param {
                Some(i) => {
                    let pattern = &signature.params[i].1;
                    self.comparing = comparing;
                    self.fit(body, &arg.value, &gives, pattern, &mut sol, vars);
                    self.comparing = false;
                }
                None if gives == Ty::Nothing => {
                    self.errors.push(self.nothing_where(&arg.value, "text"));
                }
                None => {}
            }
        }
        // A literal takes the type the other arguments settle. When they do
        // not, only the first of them is reported.
        let mut reported = false;
        let (calls, numbers): (Vec<_>, Vec<_>) =
            (deferred.into_iter()).partition(|(_, value)| matches!(value, Expr::Call(_)));
        for (i, value) in calls.into_iter().chain(numbers) {
            let pattern = &signature.params[i].1;
            let wanted = match pattern.partial(&sol) {
                // A whole number that fits an `i64` is one when nothing
                // decides, with no mistake to report, so it is given its type.
                t if t.unknown() && !pattern.partial(&from_place).unknown() => {
                    Want::Ty(pattern.partial(&from_place))
                }
                t if t.unknown() && matches!(value, Expr::Call(_)) => Want::Any,
                t if t.unknown()
                    && (fits_i64(value)
                        || matches!(value, Expr::Match(m) if literal_blocks(m))) =>
                {
                    Want::Any
                }
                t if t.unknown() && reported => Want::Ty(Ty::Unknown),
                t if t.unknown() => {
                    reported = true;
                    Want::Any
                }
                t => Want::Ty(t),
            };
            let gives = self.expr(body, value, &wanted);
            self.comparing = comparing;
            self.fit(body, value, &gives, pattern, &mut sol, vars);
            self.comparing = false;
        }
        // The two sides of a comparison that are two ranges are taken as the
        // smallest range that holds both.
        if comparing
            && let [Ty::Int(a), Ty::Int(b)] = call
                .args
                .iter()
                .map(|a| {
                    self.types
                        .get(&addr(&a.value))
                        .cloned()
                        .unwrap_or(Ty::Unknown)
                })
                .collect::<Vec<_>>()[..]
            && a != b
            && let Some(h) = a.hull(b)
        {
            for (arg, int) in call.args.iter().zip([a, b]) {
                if int != h {
                    self.widened.insert(addr(&arg.value), h);
                }
            }
        }
        // A lambda whose parameters' types nothing decided, and which writes
        // none of them.
        if self.errors.len() == errors_before
            && let Some(l) = lambdas.iter().find_map(|&(i, l)| {
                let Ty::Fn(items) = signature.params[i].1.partial(&sol) else {
                    return None;
                };
                (l.types.iter().zip(&items))
                    .any(|(t, w)| t.is_none() && *w == Ty::Unknown)
                    .then_some(l)
            })
        {
            self.errors.push(
                Error::new(
                    l.pos,
                    "nothing decides the types of this lambda's parameters, so it writes them",
                )
                .with_fix("write each type, as in `fn(v: T) { ... }`"),
            );
        }
        let missing: Vec<String> = (signature.params.iter().enumerate())
            .zip(given.iter().zip(&signature.defaults))
            .filter(|(_, (given, default))| !**given && !**default)
            .filter(|((_, (p, _)), _)| !(unknown && p.contains('@')))
            .map(|((i, (p, _)), _)| param_words(p, i))
            .collect();
        if is_print && !missing.is_empty() {
            self.errors.push(
                Error::new(call.end, "`print` needs the text to write")
                    .with_fix("write `print(\"hello\")`"),
            );
        } else if !missing.is_empty() {
            self.errors.push(Error::new(
                call.end,
                format!(
                    "`{}` needs an argument for {}",
                    call.callee,
                    sentence(missing)
                ),
            ));
        }
        sol
    }

    /// Reports each call that closes a cycle of calls between functions
    /// written on their own, in a walk of the calls from `f`. `path` is the
    /// functions being walked, and `done` those already walked.
    ///
    /// The calls in a default that a call leaves out are calls of the
    /// function the call is written in, as the default is filled in there.
    pub(super) fn recursion(&mut self, f: usize, path: &mut Set<Item>, done: &mut Set<Item>) {
        path.insert(Item::Fn(f));
        let defs = self.defs;
        let mut all = Vec::new();
        let mut cycles = Vec::new();
        // The functions passed as values, which may be called.
        let mut passed = Vec::new();
        for e in exprs(values(&defs.file.functions[f].body)) {
            if let Some(&g) = self.fn_values.get(&addr(e)) {
                passed.push((e.pos(), g));
            }
            let Expr::Call(call) = e else {
                continue;
            };
            if self.value_calls.contains(&addr(call)) {
                continue;
            }
            all.push(call);
            defs.defaults_called(call, &mut Vec::new(), &mut all, &mut cycles);
        }
        self.errors.extend(cycles.into_iter().map(default_cycle));
        let called = (all.into_iter())
            .filter(|call| call.qualifier.is_none())
            .filter_map(|call| {
                let callee = self.defs.function(call.name_pos, call.callee.as_str())?;
                Some((call.pos, callee))
            });
        let reached: Vec<(Pos, usize)> = called.chain(passed).collect();
        let own = &defs.file.functions[f];
        let tail = tail_positions(own);
        for (pos, callee) in reached {
            if callee == f && tail.contains(&pos) {
                // A call of the function from itself that is the last thing it
                // does is a loop, which the measure of its `decreases` must
                // prove ends, as `call_decreases` does where the call is checked.
                if own.decreases.is_empty() {
                    self.errors.push(
                        Error::new(
                            pos,
                            format!(
                                "`{}` calls itself, and a function that calls itself needs a `decreases`",
                                own.name
                            ),
                        )
                        .with_fix(
                            "write `decreases` after the signature, with a whole number that is smaller at each call",
                        ),
                    );
                }
            } else if path.contains(&Item::Fn(callee)) {
                self.errors.push(Error::unsupported(
                    pos,
                    "a function that calls itself, directly or through others,",
                ));
            } else if !done.contains(&Item::Fn(callee)) {
                self.recursion(callee, path, done);
            }
        }
        path.remove(&Item::Fn(f));
        done.insert(Item::Fn(f));
    }
}

/// Where the calls are in the body of `f` that are the last thing it does: the
/// call it ends with, the calls the blocks of its last `if` or `match` end
/// with, and the call a `return` gives.
fn tail_positions(f: &Function) -> Vec<Pos> {
    let mut out = Vec::new();
    tail_calls(&f.body, &mut out);
    for e in exprs(values(&f.body)) {
        if let Expr::Return { value: Some(v), .. } = e
            && let Expr::Call(c) = &**v
        {
            out.push(c.pos);
        }
    }
    out
}

/// Where the calls are that a block ends with, which are the last thing a
/// function does when the block is its body: the call it ends with, or the
/// calls the blocks of its last `if` or `match` end with, and the call a
/// `return` gives.
fn tail_calls(stmts: &[Stmt], out: &mut Vec<Pos>) {
    match stmts.last() {
        Some(Stmt::Expr(Expr::Match(m))) if matches!(m.form, Form::If { .. } | Form::Match) => {
            for arm in &m.arms {
                tail_calls(&arm.body, out);
            }
        }
        Some(Stmt::Expr(Expr::Call(c))) => out.push(c.pos),
        _ => {}
    }
}
