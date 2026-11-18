//! Ownership: which types have `Copy` and `Discard`, and the check that a
//! value without `Copy` is used at most once, that a parameter marked `take`
//! owns its argument, and that a lent value is never kept.
//!
//! The check reads the body of a function after the rest of the checker is
//! done with it, in the order the body runs, and carries the places that
//! have been given away. It reads the type of every expression from what the
//! checker recorded, so it holds no types of its own.
//!
//! A type parameter and a function have `Copy` for now, as a bound that asks
//! for it is not built yet.

use super::*;
use crate::parser::Param;

/// Stands for `Copy` in `Defs::withouts`, where each trait is named by its
/// place among the file's traits and `Copy`, which the prelude does not
/// define, has none.
pub(super) const COPY: usize = usize::MAX;
/// Stands for `Discard`, in the same way.
pub(super) const DISCARD: usize = usize::MAX - 1;

/// Whether a type has the trait `which`, `COPY` or `DISCARD`: when no
/// `without` takes it from the type, and each of its parts has it.
fn has(defs: &Defs, which: usize, ty: &Ty, open: &mut Vec<Ty>) -> bool {
    let parts: Vec<Ty> = match ty {
        Ty::Struct(s, _) if defs.withouts.contains(&(which, Def::Struct(*s))) => return false,
        Ty::Enum(e, _) if defs.withouts.contains(&(which, Def::Enum(*e))) => return false,
        Ty::Struct(s, args) => defs.fields[*s].iter().map(|t| t.subst(args)).collect(),
        Ty::Enum(e, args) => (defs.cases[*e].iter().flatten())
            .map(|t| t.subst(args))
            .collect(),
        Ty::Tuple(items) | Ty::List(items) => items.clone(),
        _ => return true,
    };
    // A type that holds itself, which is reported elsewhere, has what its
    // other parts have.
    if open.contains(ty) {
        return true;
    }
    open.push(ty.clone());
    let all = parts.iter().all(|t| has(defs, which, t, open));
    open.pop();
    all
}

/// Whether a value of the type may be duplicated.
fn copies(defs: &Defs, ty: &Ty) -> bool {
    has(defs, COPY, ty, &mut Vec::new())
}

/// A name, and the fields after it that are read from it.
type Path = (String, Vec<String>);

/// What is known at one point of the body: which places have been given
/// away, and where. A path in it is a place whose value is gone, whole or
/// in part.
type Moved = Vec<(Path, Pos)>;

/// What lends a value, if it is lent: the function, or the lambda, whose
/// parameter it is.
type Lent = Option<String>;

/// The states of the paths that reach one point, or `None` where no path
/// does, as after a `break`.
type State = Option<Moved>;

/// Where a value goes when it is given away.
#[derive(Clone, Copy)]
enum Flow<'e> {
    /// It is only looked at.
    Lend,
    /// It is given a name, which owns it unless it was lent.
    Bind,
    /// It becomes part of a bigger value, which is stored.
    Stored,
    /// It is the result of the function or the lambda.
    Returned,
    /// It is the argument of a parameter marked `take` of the function.
    Taken(&'e str),
}

/// The state where two paths meet: a place is gone if it is gone on either.
fn join(a: State, b: State) -> State {
    match (a, b) {
        (None, other) | (other, None) => other,
        (Some(mut a), Some(b)) => {
            for (path, at) in b {
                if !a.iter().any(|(p, _)| *p == path) {
                    a.push((path, at));
                }
            }
            Some(a)
        }
    }
}

struct Walk<'a> {
    defs: &'a Defs<'a>,
    types: &'a HashMap<usize, Ty>,
    f: &'a Function,
    scope: &'a Scope,
    errors: &'a mut Vec<Error>,
    /// The places already reported, so that the second look at a loop's body
    /// does not report a mistake twice.
    reported: HashSet<Pos>,
    /// The names that are lent, each with what lends it, for as long as they
    /// mean what they did when they were bound.
    lent: HashMap<String, String>,
    /// The states at each `break` of the loops being read, innermost last.
    breaks: Vec<Vec<Moved>>,
    /// The names each lambda being read defines or takes as a parameter,
    /// innermost last.
    inside: Vec<HashSet<String>>,
}

/// Checks the body of `f`. Mistakes go in `errors`.
pub(super) fn check(
    defs: &Defs,
    types: &HashMap<usize, Ty>,
    (f, scope): (&Function, &Scope),
    errors: &mut Vec<Error>,
) {
    let mut walk = Walk {
        defs,
        types,
        f,
        scope,
        errors,
        reported: HashSet::new(),
        lent: (f.params.iter().filter(|p| !p.take))
            .map(|p| (p.name.clone(), format!("`{}`", f.name)))
            .collect(),
        breaks: Vec::new(),
        inside: Vec::new(),
    };
    let flow = if f.result.is_some() {
        Flow::Returned
    } else {
        Flow::Lend
    };
    walk.block(&mut Some(Vec::new()), &f.body, flow);
}

fn written((name, fields): &Path) -> String {
    std::iter::once(name.as_str())
        .chain(fields.iter().map(String::as_str))
        .collect::<Vec<_>>()
        .join(".")
}

/// Whether one path is the other or a part of it.
fn overlap(a: &Path, b: &Path) -> bool {
    a.0 == b.0 && a.1.iter().zip(&b.1).all(|(x, y)| x == y)
}

impl<'a> Walk<'a> {
    /// The name and fields an expression reads, if it reads a place. A name
    /// that is no value, as the enum in `Pet.none` is not, is no place.
    fn place(&self, e: &Expr) -> Option<Path> {
        match e {
            Expr::Name { name, .. } => self
                .types
                .contains_key(&addr(e))
                .then(|| (name.clone(), Vec::new())),
            Expr::Field { of, name, .. } => {
                let (root, mut fields) = self.place(of)?;
                fields.push(name.clone());
                Some((root, fields))
            }
            _ => None,
        }
    }

    fn copies(&self, e: &Expr) -> bool {
        self.types
            .get(&addr(e))
            .is_none_or(|ty| copies(self.defs, ty))
    }

    fn error(&mut self, pos: Pos, message: String) -> Option<&mut Error> {
        if !self.reported.insert(pos) {
            return None;
        }
        self.errors.push(Error::new(pos, message));
        self.errors.last_mut()
    }

    /// Reads a place. Whether the value was lent is what it gives.
    fn read(&mut self, st: &mut State, e: &Expr, path: Path, flow: Flow) -> Lent {
        let Some(moved) = st else {
            return None;
        };
        let lent = self.lent.get(&path.0).cloned();
        if let Some((_, at)) = moved.iter().find(|(p, _)| overlap(p, &path)) {
            let message = format!(
                "`{}` is used here, after it was given away at {}:{}",
                written(&path),
                at.line,
                at.col
            );
            self.error(e.pos(), message);
            return lent;
        }
        if self.copies(e) {
            return lent;
        }
        // Nothing says how often a lambda is called, so a value it did not
        // define or take is not given away inside it.
        let captured = (self.inside.last()).is_some_and(|own| !own.contains(&path.0));
        if captured && lent.is_none() && !matches!(flow, Flow::Lend) {
            let what = "giving away, inside a lambda, a value the lambda did not define or take";
            if self.reported.insert(e.pos()) {
                self.errors.push(Error::unsupported(e.pos(), what));
            }
            return lent;
        }
        match flow {
            Flow::Lend => {}
            Flow::Bind if lent.is_some() => {}
            Flow::Bind => moved.push((path, e.pos())),
            _ if lent.is_some() => {
                let by = lent.clone().unwrap_or_default();
                let what = match flow {
                    Flow::Returned => "returned".to_string(),
                    Flow::Stored => "stored".to_string(),
                    Flow::Taken(callee) => {
                        format!("passed to the `take` parameter of `{callee}`")
                    }
                    Flow::Lend | Flow::Bind => unreachable!("handled above"),
                };
                let whole = if path.1.is_empty() {
                    format!("`{}` is lent to {by}", path.0)
                } else {
                    format!(
                        "`{}` is a part of `{}`, which is lent to {by}",
                        written(&path),
                        path.0
                    )
                };
                let fix = if by.starts_with("this lambda") {
                    format!(
                        "write `take` before the type of `{}` in the function type",
                        path.0
                    )
                } else {
                    format!("write `take` before the type of `{}`", path.0)
                };
                if let Some(error) = self.error(
                    e.pos(),
                    format!("{whole}, and a lent value cannot be {what}"),
                ) {
                    error.fix = Some(fix);
                }
            }
            _ => moved.push((path, e.pos())),
        }
        lent
    }

    /// Reads an expression, whose value goes where `flow` says. It gives
    /// whether the value is lent, which a binding keeps.
    fn eval(&mut self, st: &mut State, e: &Expr, flow: Flow) -> Lent {
        if st.is_none() {
            return None;
        }
        if let Some(path) = self.place(e) {
            return self.read(st, e, path, flow);
        }
        match e {
            Expr::Text { .. } | Expr::Number { .. } => None,
            Expr::Interp { parts, .. } => {
                for part in parts {
                    if let Part::Expr(value) = part {
                        self.eval(st, value, Flow::Lend);
                    }
                }
                None
            }
            Expr::Name { .. } => None,
            Expr::Field { of, .. } => {
                self.eval(st, of, Flow::Lend);
                None
            }
            Expr::Call(call) => {
                self.call(st, call);
                None
            }
            Expr::Case { payload, .. } => {
                if let Some(payload) = payload {
                    self.eval(st, payload, Flow::Stored);
                }
                None
            }
            Expr::Match(m) => self.matching(st, m, flow),
            Expr::Arith { left, right, .. } => {
                self.eval(st, left, Flow::Lend);
                self.eval(st, right, Flow::Lend);
                None
            }
            Expr::Tuple { items, .. } | Expr::List { items, .. } => {
                for item in items {
                    self.eval(st, item, Flow::Stored);
                }
                None
            }
            Expr::Index { of, index, .. } => {
                self.eval(st, of, Flow::Lend);
                self.eval(st, index, Flow::Lend);
                None
            }
            Expr::Lambda(l) => {
                self.lambda(st, l, &[]);
                None
            }
            Expr::Loop(l) => {
                self.looping(st, l);
                None
            }
            Expr::Break { .. } => {
                if let (Some(moved), Some(exits)) = (st.take(), self.breaks.last_mut()) {
                    exits.push(moved);
                }
                None
            }
            // A `return` ends the path where it is, as the function or the
            // lambda gives its value to its caller and no line after it runs.
            // The value goes where the result of the function or the lambda
            // goes, so a lent value is not returned and a value without
            // `Copy` is given away, as the last expression gives it away.
            Expr::Return { value, .. } => {
                if let Some(value) = value {
                    self.eval(st, value, Flow::Returned);
                }
                *st = None;
                None
            }
        }
    }

    /// The statements of a block, whose last gives the value that goes where
    /// `flow` says. It gives whether that value is lent.
    fn block(&mut self, st: &mut State, stmts: &[Stmt], flow: Flow) -> Lent {
        let mut lent = None;
        for (i, stmt) in stmts.iter().enumerate() {
            let last = i + 1 == stmts.len();
            match stmt {
                Stmt::Define { name, value, .. } => {
                    let l = self.eval(st, value, Flow::Bind);
                    self.bind(st, name, l);
                }
                Stmt::Assign {
                    name,
                    fields,
                    value,
                    ..
                } => {
                    self.eval(st, value, Flow::Stored);
                    if let Some(moved) = st {
                        let path: Path =
                            (name.clone(), fields.iter().map(|f| f.0.clone()).collect());
                        moved.retain(|(p, _)| !(path.0 == p.0 && p.1.starts_with(&path.1)));
                    }
                }
                Stmt::Destructure { pattern, value, .. } => {
                    let l = self.eval(st, value, Flow::Bind);
                    self.bind_pattern(st, pattern, &l);
                }
                Stmt::Expr(value) => {
                    lent = self.eval(st, value, if last { flow } else { Flow::Lend });
                }
            }
        }
        lent
    }

    /// Gives a name a new value, which is gone nowhere.
    fn bind(&mut self, st: &mut State, name: &str, lent: Lent) {
        if let Some(own) = self.inside.last_mut() {
            own.insert(name.to_string());
        }
        if let Some(moved) = st {
            moved.retain(|(p, _)| p.0 != name);
        }
        match lent {
            Some(by) => {
                self.lent.insert(name.to_string(), by);
            }
            None => {
                self.lent.remove(name);
            }
        }
    }

    fn bind_pattern(&mut self, st: &mut State, pattern: &Pattern, lent: &Lent) {
        match pattern {
            Pattern::Bind { name, .. } => self.bind(st, name, lent.clone()),
            Pattern::Case { inner, .. } => {
                if let Some(inner) = inner {
                    self.bind_pattern(st, inner, lent);
                }
            }
            Pattern::Tuple { items, .. } => {
                for item in items {
                    self.bind_pattern(st, item, lent);
                }
            }
            Pattern::Number { .. } | Pattern::Text { .. } => {}
        }
    }

    /// Whether a pattern, matched against a value of type `ty`, names a part
    /// that has no `Copy`.
    fn binds_part(&self, pattern: &Pattern, ty: &Ty) -> bool {
        match (pattern, ty) {
            (Pattern::Bind { .. }, ty) => !copies(self.defs, ty),
            (
                Pattern::Case {
                    name,
                    inner: Some(inner),
                    ..
                },
                Ty::Enum(e, args),
            ) => match self.defs.case_of(*e, name) {
                Some(c) => (self.defs.cases[*e][c].as_ref())
                    .is_some_and(|held| self.binds_part(inner, &held.subst(args))),
                None => false,
            },
            (Pattern::Tuple { items, .. }, Ty::Tuple(tys)) => {
                items.iter().zip(tys).any(|(p, t)| self.binds_part(p, t))
            }
            _ => false,
        }
    }

    fn matching(&mut self, st: &mut State, m: &Match, flow: Flow) -> Lent {
        let part = (self.types.get(&addr(&m.of))).is_some_and(|ty| {
            let ty = ty.clone();
            m.arms.iter().any(|arm| self.binds_part(&arm.pattern, &ty))
        });
        let lent_scrutinee = self.eval(st, &m.of, if part { Flow::Bind } else { Flow::Lend });
        let start = st.take();
        let mut end: State = None;
        let mut lent = None;
        for arm in &m.arms {
            let mut arm_st = start.clone();
            self.bind_pattern(&mut arm_st, &arm.pattern, &lent_scrutinee);
            lent = lent.or(self.block(&mut arm_st, &arm.body, flow));
            end = join(end, arm_st);
        }
        // A `match` with no arm, or an `if` with no `else`, runs no block
        // for the value that matches nothing.
        if m.arms.is_empty() {
            end = start;
        }
        *st = end;
        lent
    }

    fn looping(&mut self, st: &mut State, l: &Loop) {
        if let Some(range) = &l.range {
            self.eval(st, &range.from, Flow::Lend);
            self.eval(st, &range.to, Flow::Lend);
        }
        if let Some(measure) = &l.decreases {
            self.eval(st, measure, Flow::Lend);
        }
        let entry = st.clone();
        self.breaks.push(Vec::new());
        // The body is read a second time with what the first round gave
        // away, as it is read again when the loop goes round.
        let mut round = st.clone();
        if let (Some(range), Some(_)) = (&l.range, &round) {
            self.bind(&mut round, &range.name, None);
        }
        self.block(&mut round, &l.body, Flow::Lend);
        let mut again = join(entry.clone(), round.clone());
        if let (Some(range), Some(_)) = (&l.range, &again) {
            self.bind(&mut again, &range.name, None);
        }
        self.block(&mut again, &l.body, Flow::Lend);
        let mut after: State = None;
        for exit in self.breaks.pop().unwrap_or_default() {
            after = join(after, Some(exit));
        }
        if l.range.is_some() {
            after = join(after, join(entry, join(round, again)));
        }
        *st = after;
    }

    /// Reads a lambda where it is written, as the block it is, though a
    /// lambda may be called more than once: what it gives away of what it
    /// did not define or take is refused. Its parameter owns what it is
    /// given when `takes` says so.
    fn lambda(&mut self, st: &mut State, l: &Lambda, takes: &[bool]) {
        self.inside.push(HashSet::new());
        for (i, param) in l.params.iter().enumerate() {
            let lent = (!takes.get(i).copied().unwrap_or(false)).then(|| "this lambda".to_string());
            self.bind(st, &param.0, lent);
        }
        self.block(st, &l.body, Flow::Returned);
        self.inside.pop();
    }

    /// Reads a call: each argument is given away when its parameter is
    /// marked `take`, and lent otherwise, and a struct takes what it holds.
    fn call(&mut self, st: &mut State, call: &Call) {
        let defs = self.defs;
        let name = call.callee.as_str();
        // What each parameter is: marked `take`, and for a function, what
        // marks its own parameters.
        let params: Option<&[Param]> = match &call.qualifier {
            Some((q, _)) if defs.enum_names.contains_key(q.as_str()) => None,
            Some((q, _)) => (defs.trait_names.get(q.as_str())).and_then(|&k| {
                let t = &defs.file.traits[k];
                (t.functions.iter().find(|f| f.name == name)).map(|f| f.params.as_slice())
            }),
            None if self
                .f
                .params
                .iter()
                .any(|p| p.name == name && p.ty.function) =>
            {
                None
            }
            None if defs.struct_names.contains_key(name) => None,
            None => match defs.function_names.get(name) {
                Some(&f) => Some(defs.file.functions[f].params.as_slice()),
                None => match defs.trait_fns.get(name).map(Vec::as_slice) {
                    Some([(k, fi), ..]) => {
                        Some(defs.file.traits[*k].functions[*fi].params.as_slice())
                    }
                    _ => None,
                },
            },
        };
        let value_fn = self
            .f
            .params
            .iter()
            .find(|p| call.qualifier.is_none() && p.name == name && p.ty.function);
        let constructs = call
            .qualifier
            .as_ref()
            .is_some_and(|(q, _)| defs.enum_names.contains_key(q.as_str()))
            || (call.qualifier.is_none()
                && value_fn.is_none()
                && defs.struct_names.contains_key(name));
        let mut position = 0;
        let mut given: Vec<(usize, &Expr)> = Vec::new();
        for arg in &call.args {
            let ix = match (&arg.name, params) {
                (Some((n, _)), Some(ps)) => ps.iter().position(|p| p.name == *n),
                (None, Some(ps)) if arg.after => ps.len().checked_sub(1),
                (None, Some(_)) => {
                    position += 1;
                    Some(position - 1)
                }
                _ => None,
            };
            let param = ix.and_then(|i| params.and_then(|ps| ps.get(i)));
            if let Some(i) = ix {
                given.push((i, &arg.value));
            }
            let flow = match (param, value_fn) {
                _ if constructs => Flow::Stored,
                (Some(p), _) if p.take => Flow::Taken(name),
                (None, Some(f)) => {
                    let at = if arg.name.is_none() && !arg.after {
                        position += 1;
                        position - 1
                    } else {
                        usize::MAX
                    };
                    if f.takes.get(at).copied().unwrap_or(false) {
                        Flow::Taken(name)
                    } else {
                        Flow::Lend
                    }
                }
                _ => Flow::Lend,
            };
            match &arg.value {
                Expr::Lambda(l) => {
                    let takes = param.map(|p| p.takes.as_slice()).unwrap_or(&[]);
                    self.lambda(st, l, takes);
                }
                value => {
                    self.eval(st, value, flow);
                }
            }
        }
        if !constructs && value_fn.is_none() && params.is_some() {
            self.generic(call, &given);
        }
    }

    /// Refuses a call that gives a type parameter of the function, or of the
    /// implementation it chooses, a type without `Copy`, as nothing yet says
    /// that a type parameter has `Copy`, and the function may copy its value.
    fn generic(&mut self, call: &Call, given: &[(usize, &Expr)]) {
        let defs = self.defs;
        let name = call.callee.as_str();
        let of_trait = match &call.qualifier {
            Some((q, _)) => (defs.trait_names.get(q.as_str())).and_then(|&k| {
                let at = defs.file.traits[k]
                    .functions
                    .iter()
                    .position(|f| f.name == name);
                at.map(|fi| (k, fi))
            }),
            None if defs.function_names.contains_key(name) => None,
            None => match defs.trait_fns.get(name).map(Vec::as_slice) {
                Some([(k, fi), ..]) => Some((*k, *fi)),
                _ => None,
            },
        };
        let solve = |params: &[(String, Ty)], gives: &Ty, vars: usize| {
            let mut sol: Vec<Option<Ty>> = vec![None; vars];
            for (i, value) in given {
                if let (Some(t), Some((_, p))) = (self.types.get(&addr(*value)), params.get(*i))
                    && !t.unknown()
                {
                    unify(p, t, &mut sol);
                }
            }
            if let Some(t) = self.types.get(&addr(call))
                && !t.unknown()
            {
                unify(gives, t, &mut sol);
            }
            sol
        };
        if let Some((k, fi)) = of_trait {
            let sig = &defs.trait_sigs[k][fi];
            let sol = solve(&sig.params, &sig.gives, defs.file.traits[k].generics.len());
            let outs = &defs.outs[k];
            if sol.iter().zip(outs).any(|(t, out)| t.is_none() && !out) {
                return;
            }
            let types: Vec<Ty> = (sol.into_iter())
                .map(|t| t.unwrap_or(Ty::Unknown))
                .collect();
            if let Found::Yes(_, traits::By::Impl(_, chosen)) =
                defs.find(k, &types, self.scope, &mut Vec::new())
                && chosen.iter().any(|t| !copies(defs, t))
            {
                let what = format!(
                    "an implementation of `{}` chosen for a type without `Copy`",
                    defs.file.traits[k].name
                );
                self.errors.push(Error::unsupported(call.pos, &what));
            }
        } else if let Some(&f) = defs.function_names.get(name) {
            let sig = &defs.signatures[f];
            let sol = solve(&sig.params, &sig.gives, sig.scope.names.len());
            let bad = sol
                .iter()
                .enumerate()
                .find(|(_, t)| t.as_ref().is_some_and(|t| !copies(defs, t)));
            if let Some((i, _)) = bad {
                let what = format!(
                    "a type without `Copy` for the type parameter `{}` of `{name}`",
                    sig.scope.names[i]
                );
                self.errors.push(Error::unsupported(call.pos, &what));
            }
        }
    }
}
