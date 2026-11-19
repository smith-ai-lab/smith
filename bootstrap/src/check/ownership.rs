//! Ownership: which types have `Copy` and `Discard`, and the check that a
//! value without `Copy` is used at most once, that a parameter marked `take`
//! owns its argument, and that a lent value is never kept.
//!
//! The check reads the body of a function after the rest of the checker is
//! done with it, in the order the body runs, and carries the places that
//! have been given away. It reads the type of every expression from what the
//! checker recorded, so it holds no types of its own.
//!
//! Nothing is known of a type parameter but what its bounds say, so it has
//! `Copy` and `Discard` only where a bound asks. A function has `Copy` for
//! now.

use super::*;
use crate::parser::Param;

/// Stands for `Copy` in `Defs::withouts`, where each trait is named by its
/// place among the file's traits and `Copy`, which the prelude does not
/// define, has none.
pub(super) const COPY: usize = usize::MAX;
/// Stands for `Discard`, in the same way.
pub(super) const DISCARD: usize = usize::MAX - 1;

/// Whether a type has the trait `which`, `COPY` or `DISCARD`: when no
/// `without` takes it from the type, each of its parts has it, and each type
/// parameter in it has it by a bound of `scope`. When it has not, `Err`
/// gives the first type parameter that lacks it, if that is why.
fn has(
    (defs, scope): (&Defs, &Scope),
    which: usize,
    ty: &Ty,
    open: &mut Vec<Ty>,
) -> Result<(), Option<usize>> {
    let parts: Vec<Ty> = match ty {
        Ty::Param(i) => {
            let flags = if which == COPY {
                &scope.copy
            } else {
                &scope.discard
            };
            return if flags.get(*i).copied().unwrap_or(true) {
                Ok(())
            } else {
                Err(Some(*i))
            };
        }
        Ty::Struct(s, _) if defs.withouts.contains(&(which, Def::Struct(*s))) => return Err(None),
        Ty::Enum(e, _) if defs.withouts.contains(&(which, Def::Enum(*e))) => return Err(None),
        Ty::Struct(s, args) => defs.fields[*s].iter().map(|t| t.subst(args)).collect(),
        Ty::Enum(e, args) => (defs.cases[*e].iter().flatten())
            .map(|t| t.subst(args))
            .collect(),
        // A list is never copied unasked, whatever its elements are.
        Ty::List(_) if which == COPY => return Err(None),
        Ty::Tuple(items) | Ty::List(items) => items.clone(),
        _ => return Ok(()),
    };
    // A type that holds itself, which is reported elsewhere, has what its
    // other parts have.
    if open.contains(ty) {
        return Ok(());
    }
    open.push(ty.clone());
    let mut all = Ok(());
    for t in &parts {
        match has((defs, scope), which, t, open) {
            Err(Some(i)) => {
                all = Err(Some(i));
                break;
            }
            Err(None) => all = Err(None),
            Ok(()) => {}
        }
    }
    open.pop();
    all
}

/// Whether a value of the type may be duplicated.
pub(super) fn copies(defs: &Defs, scope: &Scope, ty: &Ty) -> bool {
    has((defs, scope), COPY, ty, &mut Vec::new()).is_ok()
}

/// The type parameter, if there is one, that a type lacks `which` because of.
fn lacking(defs: &Defs, scope: &Scope, which: usize, ty: &Ty) -> Option<usize> {
    has((defs, scope), which, ty, &mut Vec::new())
        .err()
        .flatten()
}

/// Why a type has no `Discard`: `None` when it has it, and otherwise the type
/// parameter that lacks it, if that is why.
fn undiscardable(defs: &Defs, scope: &Scope, ty: &Ty) -> Option<Option<usize>> {
    has((defs, scope), DISCARD, ty, &mut Vec::new()).err()
}

/// What follows `what is dropped, as nothing takes it`: which type cannot be
/// dropped, and the fix.
fn why_dropped(defs: &Defs, scope: &Scope, ty: &Ty, cause: Option<usize>) -> (String, String) {
    match cause {
        Some(p) => {
            let param = &scope.names[p];
            (
                format!(", and `{param}` has no `Discard`"),
                format!("write `{param}: Discard`"),
            )
        }
        None => (
            format!(
                ", and a `{}` cannot be dropped, as it has no `Discard`",
                defs.text(ty, scope)
            ),
            "give it to a function that takes it, or return it".to_string(),
        ),
    }
}

/// A name, and the fields after it that are read from it.
type Path = (String, Vec<String>);

/// A value the function owns and has not taken on some path, whose type has
/// no `Discard`, because of a type parameter or a `without`.
#[derive(Clone, Debug)]
struct Held {
    name: String,
    /// The fields after the name that the value is a part by: none for the
    /// whole of what the name holds, and the parts left when others were taken.
    fields: Vec<String>,
    pos: Pos,
    ty: Ty,
}

/// What is known at one point of the body: which places have been given
/// away, and where, and which values may not have been used yet. A path in
/// `gone` is a place whose value is gone, whole or in part.
#[derive(Clone, Debug, Default)]
struct Moved {
    gone: Vec<(Path, Pos)>,
    held: Vec<Held>,
}

/// What a lambda that is being read defines or takes as a parameter, and the
/// names it gives away that it did not.
#[derive(Default)]
struct Frame {
    /// What was held where the lambda began, which its `return` does not drop.
    outer: Vec<(String, Pos)>,
    own: HashSet<String>,
    gives: Vec<(String, Pos, Option<Ty>)>,
}

/// Whether the parameter that receives a lambda owns it.
#[derive(Clone, Copy, PartialEq)]
enum Kept {
    /// It is marked `take`, so the function is called once.
    Taken,
    /// It lends the function, so the function may be called any number of
    /// times.
    Lent,
}

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

/// The most rounds a loop's body is read, which is far more than it needs.
const ROUNDS: usize = 1000;

/// Whether two states know the same: the same places gone, and the same
/// values held.
fn same(a: &State, b: &State) -> bool {
    match (a, b) {
        (None, None) => true,
        (Some(a), Some(b)) => {
            let held = |x: &Moved, y: &Moved| {
                x.held.iter().all(|h| {
                    (y.held.iter())
                        .any(|g| g.name == h.name && g.pos == h.pos && g.fields == h.fields)
                })
            };
            let gone = |x: &Moved, y: &Moved| {
                x.gone
                    .iter()
                    .all(|(p, _)| y.gone.iter().any(|(q, _)| p == q))
            };
            held(a, b) && held(b, a) && gone(a, b) && gone(b, a)
        }
        _ => false,
    }
}

/// The state where two paths meet: a place is gone if it is gone on either.
fn join(a: State, b: State) -> State {
    match (a, b) {
        (None, other) | (other, None) => other,
        (Some(mut a), Some(b)) => {
            for (path, at) in b.gone {
                if !a.gone.iter().any(|(p, _)| *p == path) {
                    a.gone.push((path, at));
                }
            }
            for h in b.held {
                if !a
                    .held
                    .iter()
                    .any(|g| g.name == h.name && g.pos == h.pos && g.fields == h.fields)
                {
                    a.held.push(h);
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
    /// The lambdas being read, innermost last.
    inside: Vec<Frame>,
    /// The names that were used after they were given away.
    reused: HashSet<String>,
}

/// Checks the body of `f`. Mistakes go in `errors`.
pub(super) fn check(
    defs: &Defs,
    types: &HashMap<usize, Ty>,
    (f, scope, params): (&Function, &Scope, &[(String, Ty)]),
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
        reused: HashSet::new(),
    };
    let flow = if f.result.is_some() {
        Flow::Returned
    } else {
        Flow::Lend
    };
    let mut st = Some(Moved::default());
    for p in f.params.iter().filter(|p| p.take) {
        if let Some((_, ty)) = params.iter().find(|(n, _)| *n == p.name) {
            walk.own(&mut st, &p.name, p.pos, ty);
        }
    }
    walk.block(&mut st, &f.body, flow);
    walk.dropped(&st);
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

    /// The parameter of the function that a path is, if it is of a function
    /// type.
    fn fn_param(&self, path: &Path) -> Option<&'a Param> {
        let f: &'a Function = self.f;
        let own = path.1.is_empty() && self.inside.iter().all(|fr| !fr.own.contains(&path.0));
        (own.then(|| f.params.iter().find(|p| p.name == path.0 && p.ty.function))).flatten()
    }

    fn copies(&self, e: &Expr) -> bool {
        (self.types.get(&addr(e))).is_none_or(|ty| copies(self.defs, self.scope, ty))
    }

    /// Notes that a value was bound that must be taken, if its type has no
    /// `Discard`.
    fn own(&mut self, st: &mut State, name: &str, pos: Pos, ty: &Ty) {
        if self.lent.contains_key(name) || undiscardable(self.defs, self.scope, ty).is_none() {
            return;
        }
        if let Some(moved) = st {
            let held = Held {
                name: name.to_string(),
                fields: Vec::new(),
                pos,
                ty: ty.clone(),
            };
            moved.held.retain(|h| h.name != name);
            moved.held.push(held);
        }
    }

    /// Reports each value that may be dropped where `st` is, as the function
    /// ends there, or as the lambda that returns does, which drops only what
    /// it holds itself.
    fn dropped(&mut self, st: &State) {
        let Some(moved) = st else {
            return;
        };
        let outer = self.inside.last().map(|f| f.outer.clone());
        for h in &moved.held {
            if outer
                .as_ref()
                .is_some_and(|o| o.iter().any(|(n, p)| *n == h.name && *p == h.pos))
            {
                continue;
            }
            let Some(cause) = undiscardable(self.defs, self.scope, &h.ty) else {
                continue;
            };
            let (why, fix) = why_dropped(self.defs, self.scope, &h.ty, cause);
            let place = written(&(h.name.clone(), h.fields.clone()));
            let message = format!("`{place}` is dropped here, as nothing takes it{why}");
            if let Some(error) = self.error(h.pos, message) {
                error.fix = Some(fix);
            }
        }
    }

    /// Reports a value that nothing uses, as the result of a call that is
    /// a statement of its own is, when its type has no `Discard`.
    fn temporary(&mut self, e: &Expr) {
        let made = matches!(
            e,
            Expr::Call(_) | Expr::Tuple { .. } | Expr::List { .. } | Expr::Case { .. }
        );
        if !made {
            return;
        }
        let Some(ty) = self.types.get(&addr(e)) else {
            return;
        };
        let Some(cause) = undiscardable(self.defs, self.scope, ty) else {
            return;
        };
        let (why, fix) = why_dropped(self.defs, self.scope, ty, cause);
        let message = format!("this value is dropped, as nothing takes it{why}");
        if let Some(error) = self.error(e.pos(), message) {
            error.fix = Some(fix);
        }
    }

    /// Notes each name a pattern binds to a part of a value of type `ty`.
    fn own_pattern(&mut self, st: &mut State, pattern: &Pattern, ty: &Ty) {
        match (pattern, ty) {
            (Pattern::Bind { name, pos, .. }, ty) => self.own(st, name, *pos, ty),
            (
                Pattern::Case {
                    name,
                    inner: Some(inner),
                    ..
                },
                Ty::Enum(e, args),
            ) => {
                if let Some(c) = self.defs.case_of(*e, name)
                    && let Some(held) = self.defs.cases[*e][c].as_ref()
                {
                    self.own_pattern(st, inner, &held.subst(args));
                }
            }
            (Pattern::Tuple { items, .. }, Ty::Tuple(tys)) => {
                for (p, t) in items.iter().zip(tys) {
                    self.own_pattern(st, p, t);
                }
            }
            _ => {}
        }
    }

    /// What is left of `h` to take when its part at `fields` is taken: each
    /// other field, along the way to it, whose type has no `Discard`.
    fn leftover(&self, h: &Held, fields: &[String]) -> Vec<Held> {
        let mut left = Vec::new();
        let mut at = h.fields.clone();
        let mut ty = h.ty.clone();
        for field in &fields[h.fields.len()..] {
            let Ty::Struct(s, args) = &ty else {
                break;
            };
            let names = &self.defs.file.structs[*s].fields;
            let mut inner = None;
            for (f, t) in names.iter().zip(&self.defs.fields[*s]) {
                let t = t.subst(args);
                if f.name == *field {
                    inner = Some(t);
                } else if undiscardable(self.defs, self.scope, &t).is_some() {
                    let mut of = at.clone();
                    of.push(f.name.clone());
                    left.push(Held {
                        name: h.name.clone(),
                        fields: of,
                        pos: h.pos,
                        ty: t,
                    });
                }
            }
            let Some(inner) = inner else {
                break;
            };
            at.push(field.clone());
            ty = inner;
        }
        left
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
        // A value is taken where a value without `Copy` would be given away:
        // looking at it, lending it, and reading a part of it take nothing.
        // A part that has no `Discard` is taken, and each other part of what
        // holds it that has none is left to be taken.
        let part = (!path.1.is_empty() && !matches!(flow, Flow::Lend))
            .then(|| self.types.get(&addr(e)))
            .flatten()
            .is_some_and(|ty| undiscardable(self.defs, self.scope, ty).is_some());
        if !matches!(flow, Flow::Lend) && path.1.is_empty() {
            moved.held.retain(|h| h.name != path.0);
        }
        if part && !moved.gone.iter().any(|(p, _)| overlap(p, &path)) {
            let mut parts = Vec::new();
            moved.held.retain(|h| {
                let covers = h.name == path.0 && path.1.starts_with(&h.fields);
                if covers {
                    parts.push(h.clone());
                }
                !covers
            });
            for h in parts {
                moved.held.extend(self.leftover(&h, &path.1));
            }
        }
        if let Some((_, at)) = moved.gone.iter().find(|(p, _)| overlap(p, &path)) {
            let message = format!(
                "`{}` is used here, after it was given away at {}:{}",
                written(&path),
                at.line,
                at.col
            );
            let ty = self.types.get(&addr(e));
            let blame = ty.and_then(|ty| lacking(self.defs, self.scope, COPY, ty));
            let duplicable = match ty {
                Some(Ty::List(of)) => copies(self.defs, self.scope, &of[0]),
                _ => false,
            };
            let fix = match blame {
                Some(p) => Some(format!("write `{}: Copy`", self.scope.names[p])),
                None if duplicable => Some(format!(
                    "write `copy({})` where two lists are meant",
                    written(&path)
                )),
                None => None,
            };
            self.reused.insert(path.0.clone());
            if let Some(error) = self.error(e.pos(), message) {
                error.fix = fix;
            }
            return lent;
        }
        // A function that is called once has no `Copy`. A lent function has,
        // but it is not given to a parameter that takes its function.
        let function = self.fn_param(&path);
        let once = function.is_some_and(|p| p.take);
        let lent_function = function.is_some_and(|p| !p.take) && matches!(flow, Flow::Taken(_));
        if once && matches!(flow, Flow::Lend) {
            let message = format!(
                "`{}` is given to a parameter that may call it more than once, and it can be called once",
                path.0
            );
            if let Some(moved) = st {
                moved.held.retain(|h| h.name != path.0);
            }
            if let Some(error) = self.error(e.pos(), message) {
                error.fix =
                    Some("write `take` before the function type of that parameter".to_string());
            }
            return lent;
        }
        if !once && !lent_function && self.copies(e) {
            return lent;
        }
        // A lambda gives away what it did not define or take, and the
        // lambda is spent by its call, which the parameter it is passed to
        // must say.
        if lent.is_none()
            && !matches!(flow, Flow::Lend)
            && let Some(frame) = self.inside.last_mut()
            && !frame.own.contains(&path.0)
        {
            frame
                .gives
                .push((path.0.clone(), e.pos(), self.types.get(&addr(e)).cloned()));
        }
        match flow {
            Flow::Lend => {}
            Flow::Bind if lent.is_some() => {}
            Flow::Bind => moved.gone.push((path, e.pos())),
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
                ) && by != "this `loop`"
                {
                    error.fix = Some(fix);
                }
            }
            _ => moved.gone.push((path, e.pos())),
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
        // A value that is made and only looked at is dropped.
        if matches!(flow, Flow::Lend) {
            self.temporary(e);
        }
        match e {
            Expr::Text { .. } | Expr::Number { .. } | Expr::Unbuilt { .. } => None,
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
            Expr::Slice { of, from, to, .. } => {
                self.eval(st, of, Flow::Lend);
                for end in [from, to].into_iter().flatten() {
                    self.eval(st, end, Flow::Lend);
                }
                None
            }
            Expr::Lambda(l) => {
                self.lambda(st, l, &[], Kept::Lent);
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
                self.dropped(st);
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
                Stmt::Define {
                    name, value, pos, ..
                } => {
                    let l = self.eval(st, value, Flow::Bind);
                    self.bind(st, name, l);
                    if let Some(ty) = self.types.get(&addr(value)) {
                        self.own(st, name, *pos, &ty.clone());
                    }
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
                        // The value a field held is dropped by the assignment,
                        // unless it was taken.
                        let kept = !moved.gone.iter().any(|(p, _)| overlap(p, &path));
                        let old = self.types.get(&addr(value)).cloned();
                        let cause = old.and_then(|ty| {
                            let cause = undiscardable(self.defs, self.scope, &ty)?;
                            Some((ty, cause))
                        });
                        if let (true, Some((ty, cause)), Some(at)) = (kept, cause, fields.last()) {
                            let (why, fix) = why_dropped(self.defs, self.scope, &ty, cause);
                            let message = format!(
                                "`{}` is dropped here, as the assignment replaces it and nothing takes it{why}",
                                written(&path)
                            );
                            if let Some(error) = self.error(at.1, message) {
                                error.fix = Some(fix);
                            }
                        }
                        let Some(moved) = st else {
                            continue;
                        };
                        // A part that was taken and is assigned again is
                        // there to be taken again.
                        let retaken = moved.gone.iter().any(|(p, _)| *p == path);
                        if let (true, Some(ty), Some(at)) =
                            (retaken, self.types.get(&addr(value)), fields.last())
                            && undiscardable(self.defs, self.scope, ty).is_some()
                        {
                            moved.held.push(Held {
                                name: path.0.clone(),
                                fields: path.1.clone(),
                                pos: at.1,
                                ty: ty.clone(),
                            });
                        }
                        moved
                            .gone
                            .retain(|(p, _)| !(path.0 == p.0 && p.1.starts_with(&path.1)));
                    }
                }
                Stmt::Destructure { pattern, value, .. } => {
                    let l = self.eval(st, value, Flow::Bind);
                    self.bind_pattern(st, pattern, &l);
                    if let Some(ty) = self.types.get(&addr(value)) {
                        self.own_pattern(st, pattern, &ty.clone());
                    }
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
        if let Some(frame) = self.inside.last_mut() {
            frame.own.insert(name.to_string());
        }
        // Every value the name may hold is dropped, as paths that met may
        // hold different ones.
        let old: Vec<Held> = (st.as_ref())
            .map(|m| m.held.iter().filter(|h| h.name == name).cloned().collect())
            .unwrap_or_default();
        if !old.is_empty() {
            self.dropped(&Some(Moved {
                gone: Vec::new(),
                held: old,
            }));
        }
        if let Some(moved) = st {
            moved.gone.retain(|(p, _)| p.0 != name);
            moved.held.retain(|h| h.name != name);
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
            (Pattern::Bind { .. }, ty) => !copies(self.defs, self.scope, ty),
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
            if let Some(ty) = self.types.get(&addr(&m.of)) {
                self.own_pattern(&mut arm_st, &arm.pattern, &ty.clone());
            }
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
        if let Some(over) = &l.over {
            self.eval(st, &over.value, Flow::Lend);
        }
        if let Some(measure) = &l.decreases {
            self.eval(st, measure, Flow::Lend);
        }
        // The elements stay in the list, so the block is lent each one.
        let element = "this `loop`";
        let entry = st.clone();
        let before: Vec<Held> = entry.as_ref().map(|m| m.held.clone()).unwrap_or_default();
        let reused = self.reused.clone();
        self.breaks.push(Vec::new());
        // The body is read again, from what is known at the head of the loop
        // when the loop goes round, until that stops changing. What is known
        // is a set of places given away and a set of values held, `join` only
        // adds to them, and each is among the names, fields and places of the
        // function, so it stops growing after a number of rounds bounded by
        // their count. The limit is far above that, so that a mistake in
        // `join` is a failure and not a loop that never ends. A mistake is
        // reported once, however many rounds read it. A value is known by the
        // statement that made it and not by where it has moved to, so a chain
        // of names that a round moves a value along is known after one round,
        // and no program built for this has needed more than three.
        let mut rounds: Vec<State> = Vec::new();
        let mut head = st.clone();
        loop {
            assert!(
                rounds.len() < ROUNDS,
                "what is known at a loop's head never settles"
            );
            let known = self.reused.len();
            let mut round = head.clone();
            if let (Some(range), Some(_)) = (&l.range, &round) {
                self.bind(&mut round, &range.name, None);
            }
            if let (Some(over), Some(_)) = (&l.over, &round) {
                self.bind(&mut round, &over.name, Some(element.to_string()));
            }
            self.block(&mut round, &l.body, Flow::Lend);
            let wider = join(head.clone(), round.clone());
            rounds.push(round);
            // The names used after they were given away are known too, as a
            // loop in the body reads them to tell what it is the first to find.
            if same(&wider, &head) && self.reused.len() == known {
                break;
            }
            head = wider;
        }
        let mut after: State = None;
        for exit in self.breaks.pop().unwrap_or_default() {
            after = join(after, Some(exit));
        }
        if l.range.is_some() || l.over.is_some() {
            after = join(after, entry);
            for round in rounds {
                after = join(after, round);
            }
        }
        // A value the loop was entered with and gives away again in each
        // round is reported there, and not also as dropped before the first.
        if let Some(moved) = &mut after {
            moved.held.retain(|h| {
                let again = self.reused.contains(&h.name) && !reused.contains(&h.name);
                !(again && before.iter().any(|b| b.name == h.name && b.pos == h.pos))
            });
        }
        *st = after;
    }

    /// Reads a lambda where it is written, as the block it is. What it gives
    /// away of what it did not define or take is spent from here, and the
    /// lambda is called once, so it is passed to a parameter that takes its
    /// function. Its parameter owns what it is given when `takes` says so.
    fn lambda(&mut self, st: &mut State, l: &Lambda, takes: &[bool], kept: Kept) {
        let outer = st
            .as_ref()
            .map(|m| m.held.iter().map(|h| (h.name.clone(), h.pos)).collect())
            .unwrap_or_default();
        self.inside.push(Frame {
            outer,
            ..Frame::default()
        });
        for (i, param) in l.params.iter().enumerate() {
            let taken = takes.get(i).copied().unwrap_or(false);
            let lent = (!taken).then(|| "this lambda".to_string());
            self.bind(st, &param.0, lent);
            // What a parameter that is taken is given must be taken.
            if let (true, Some(ty)) = (taken, self.types.get(&addr(param))) {
                self.own(st, &param.0, param.1, &ty.clone());
            }
        }
        self.block(st, &l.body, Flow::Returned);
        // What the lambda holds ends with it.
        self.dropped(st);
        if let (Some(moved), Some(frame)) = (st.as_mut(), self.inside.last()) {
            moved
                .held
                .retain(|h| frame.outer.iter().any(|(n, p)| *n == h.name && *p == h.pos));
        }
        let frame = self.inside.pop().unwrap_or_default();
        if frame.gives.is_empty() {
            return;
        }
        if kept == Kept::Lent {
            let mut names: Vec<String> = Vec::new();
            for (name, ..) in &frame.gives {
                if !names.contains(name) {
                    names.push(name.clone());
                }
            }
            let names = names.iter().map(|n| format!("`{n}`")).collect::<Vec<_>>();
            let message = format!(
                "this lambda gives away {}, and the function it is passed to may call it more than once",
                names.join(" and ")
            );
            if let Some(error) = self.error(l.pos, message) {
                error.fix =
                    Some("write `take` before the function type of that parameter".to_string());
            }
            return;
        }
        // A function that is taken is called at most once, so it may not call
        // the lambda, and what the lambda gives away is dropped.
        let lacks = frame.gives.iter().find_map(|(name, _, ty)| {
            let ty = ty.as_ref()?;
            let cause = undiscardable(self.defs, self.scope, ty)?;
            Some((name.clone(), ty.clone(), cause))
        });
        if let Some((name, ty, cause)) = lacks {
            let (why, fix) = why_dropped(self.defs, self.scope, &ty, cause);
            let message = format!(
                "this lambda gives away `{name}`, and the function it is passed to may not call it, which drops `{name}`{why}"
            );
            if let Some(error) = self.error(l.pos, message) {
                error.fix = Some(fix);
            }
        }
        // A lambda inside a lambda that gives a name away is called once too.
        if let Some(outer) = self.inside.last_mut() {
            for give in frame.gives {
                if !outer.own.contains(&give.0) {
                    outer.gives.push(give);
                }
            }
        }
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
                traits::trait_function(t, name).map(|fi| t.functions[fi].params.as_slice())
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
        if let Some(f) = value_fn.filter(|f| f.take) {
            self.spend(st, &f.name, call.pos);
        }
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
            // A slice cannot change, so it is not given where a list is
            // taken, which the callee may grow.
            if param.is_some_and(|p| p.take && p.ty.name == "List")
                && matches!(self.types.get(&addr(&arg.value)), Some(Ty::Slice(_)))
                && let Some(error) = self.error(
                    arg.value.pos(),
                    format!(
                        "`{}` takes a list, which it may grow, and this is a slice, which cannot change",
                        call.callee
                    ),
                )
            {
                error.fix = Some(format!(
                    "write `copy({})`, which is a new list",
                    super::written_expr(&arg.value)
                ));
            }
            let flow = match (param, value_fn) {
                _ if constructs => Flow::Stored,
                // `push` takes its list and its item, `append` its list.
                _ if super::lists_and_text::gives_away(defs, call, arg) => Flow::Taken(name),
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
                    let kept = if param.is_some_and(|p| p.take) {
                        Kept::Taken
                    } else {
                        Kept::Lent
                    };
                    self.lambda(st, l, takes, kept);
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

    /// Reads the call of a function that is called once: its first call
    /// spends it, and a second is a second use of a value given away.
    fn spend(&mut self, st: &mut State, name: &str, pos: Pos) {
        let Some(moved) = st else {
            return;
        };
        if let Some((_, at)) = moved.gone.iter().find(|(p, _)| p.0 == name) {
            let message = format!(
                "`{name}` is called here, after it was given away at {}:{}",
                at.line, at.col
            );
            if let Some(error) = self.error(pos, message) {
                error.fix = Some(format!(
                    "call `{name}` once, or write `{name}` without `take` so that it may be called again"
                ));
            }
            return;
        }
        moved.gone.push(((name.to_string(), Vec::new()), pos));
        if let Some(frame) = self.inside.last_mut()
            && !frame.own.contains(name)
        {
            frame.gives.push((name.to_string(), pos, None));
        }
    }

    /// Refuses a call that gives a type parameter of the function, or of the
    /// implementation it chooses, a type that lacks what a bound of the
    /// parameter asks: `Copy` or `Discard`. One mistake is reported for the
    /// call.
    fn generic(&mut self, call: &Call, given: &[(usize, &Expr)]) {
        let defs = self.defs;
        let name = call.callee.as_str();
        let of_trait = match &call.qualifier {
            Some((q, _)) => (defs.trait_names.get(q.as_str())).and_then(|&k| {
                let at = traits::trait_function(&defs.file.traits[k], name);
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
        // The scope that bounds the types, with the types it is given, and
        // what it is, as an error names it.
        let (bounds, sol, what): (&Scope, Vec<Option<Ty>>, String) = if let Some((k, fi)) = of_trait
        {
            let sig = &defs.trait_sigs[k][fi];
            let mut sol = solve(&sig.params, &sig.gives, sig.scope.names.len());
            // The function's own type parameters follow the trait's types.
            let own = sol.split_off(defs.file.traits[k].generics.len());
            let outs = &defs.outs[k];
            if sol.iter().zip(outs).any(|(t, out)| t.is_none() && !out) {
                return;
            }
            let n = sol.len();
            let types: Vec<Ty> = (sol.into_iter())
                .map(|t| t.unwrap_or(Ty::Unknown))
                .collect();
            let what = format!(
                "the implementation of `{}` chosen here",
                defs.file.traits[k].name
            );
            match defs.find(k, &types, self.scope, &mut Vec::new()) {
                Found::Yes(_, traits::By::Impl(i, chosen)) => {
                    let Some(info) = &defs.impls[i] else {
                        return;
                    };
                    let mut sol: Vec<Option<Ty>> = chosen.into_iter().map(Some).collect();
                    sol.extend(own);
                    (&info.fn_scopes[fi], sol, what)
                }
                // A bound promises the implementation, and the function's own
                // type parameters are bounded as the trait says.
                Found::Yes(..) if !own.is_empty() => {
                    let mut sol: Vec<Option<Ty>> = vec![None; n];
                    sol.extend(own);
                    (&sig.scope, sol, what)
                }
                _ => return,
            }
        } else if let Some(&f) = defs.function_names.get(name) {
            let sig = &defs.signatures[f];
            let sol = solve(&sig.params, &sig.gives, sig.scope.names.len());
            (&sig.scope, sol, format!("`{name}`"))
        } else {
            return;
        };
        for (i, t) in sol.iter().enumerate() {
            let Some(t) = t else {
                continue;
            };
            let mut lacks: Vec<&str> = Vec::new();
            if bounds.copy.get(i).copied().unwrap_or(false) && !copies(defs, self.scope, t) {
                lacks.push("Copy");
            }
            let discards = has((defs, self.scope), DISCARD, t, &mut Vec::new()).is_ok();
            if bounds.discard.get(i).copied().unwrap_or(false) && !discards {
                lacks.push("Discard");
            }
            if lacks.is_empty() {
                continue;
            }
            let param = bounds.names.get(i).cloned().unwrap_or_default();
            let message = format!(
                "`{}` has no {}, which {what} needs of `{param}`",
                defs.text(t, self.scope),
                lacks
                    .iter()
                    .map(|l| format!("`{l}`"))
                    .collect::<Vec<_>>()
                    .join(" and ")
            );
            // A type that lacks it only because of a type parameter of the
            // caller is mended by a bound there.
            let fix = lacks.iter().find_map(|l| {
                let which = if *l == "Copy" { COPY } else { DISCARD };
                let p = lacking(defs, self.scope, which, t)?;
                Some(format!("write `{}: {l}`", self.scope.names[p]))
            });
            if let Some(error) = self.error(call.pos, message) {
                error.fix = fix;
            }
            return;
        }
    }
}
