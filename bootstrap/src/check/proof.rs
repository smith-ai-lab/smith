//! What `arith` is given, and what it is asked, when a plain `+`, `-`, or `*`
//! on whole numbers is checked.
//!
//! The facts in scope are the condition of each `if` the operation is inside,
//! which holds in the first block and is false in the `else`, and what each
//! binding that is not a `var` was given. The range of each value's type and
//! the value of each literal are added when a goal is asked. A `var` can be
//! given another value between a fact and a use, so nothing is known of one
//! but the range of its type.

use std::collections::{BTreeMap, HashMap};

use super::arith::{Formula, Lin, proves};
use super::{
    Body, Checker, Int, Numbers, Signature, Ty, addr, params_of, written_expr, written_with,
};
use crate::diag::Error;
use crate::parser::{ArithOp, Call, Expr, Form, Function, Pattern, Stmt};

/// What a variable of `arith` stands for.
#[derive(Clone, Debug, PartialEq, Eq, Hash)]
pub(super) enum Key {
    /// A name that is not a `var`, by its place among the names, with the
    /// range of its type.
    Name(usize, i128, i128),
    /// A field read from such a name, written out, with its range.
    Field(String, i128, i128),
    /// A value nothing is known of but the range of its type: a `var`, or
    /// what a call gives. Each is its own.
    Fresh(usize, i128, i128),
    /// What a call gave, by the place of the call, with the range of its
    /// type. The call is one value, so every fact about it is about the same
    /// variable.
    Call(usize, i128, i128),
    /// A product of two values that are not constants, as it is written in
    /// the variables it multiplies. Nothing is known of it but the facts
    /// that are about the same product.
    Product(String),
}

impl Key {
    fn range(&self) -> Option<(i128, i128)> {
        match self {
            Key::Name(_, lo, hi)
            | Key::Field(_, lo, hi)
            | Key::Fresh(_, lo, hi)
            | Key::Call(_, lo, hi) => Some((*lo, *hi)),
            Key::Product(_) => None,
        }
    }
}

/// What `arith` has been told in a body, and the variables it has.
#[derive(Default)]
pub(super) struct Facts {
    known: Vec<Formula>,
    /// The names, by their place among the names, that were given a number
    /// that is the same every time, with the number.
    constants: Vec<(usize, i128)>,
    keys: Vec<Key>,
    ids: HashMap<Key, usize>,
    fresh: usize,
    /// While a function's contract is read at a call, the sum each of its
    /// parameters, and its result, stands for there.
    subst: Option<HashMap<String, Lin>>,
}

impl Facts {
    /// How much is known now, to go back to when a block ends.
    pub(super) fn mark(&self) -> (usize, usize) {
        (self.known.len(), self.constants.len())
    }

    /// Forgets what was learned after the mark.
    pub(super) fn reset(&mut self, mark: (usize, usize)) {
        self.known.truncate(mark.0);
        self.constants.truncate(mark.1);
    }

    fn id(&mut self, key: Key) -> usize {
        if let Some(id) = self.ids.get(&key) {
            return *id;
        }
        self.keys.push(key.clone());
        self.ids.insert(key, self.keys.len() - 1);
        self.keys.len() - 1
    }
}

/// Turns expressions into sums of variables.
struct Builder<'b, 'a> {
    body: &'b Body<'a>,
    types: &'b HashMap<usize, Ty>,
    facts: &'b mut Facts,
    /// Whether an expression arith knows nothing of is a variable of its own,
    /// as it is when a goal is asked, and not left out, as it is for a fact.
    goal: bool,
    /// Whether a number grew too large to hold.
    overflow: bool,
}

fn constant(n: i128) -> Lin {
    Lin {
        terms: BTreeMap::new(),
        constant: n,
    }
}

impl Builder<'_, '_> {
    fn range_of(&self, e: &Expr) -> (i128, i128) {
        match self.types.get(&addr(e)) {
            Some(Ty::Int(t)) => (t.min(), t.max()),
            _ => (i128::MIN / 4, i128::MAX / 4),
        }
    }

    fn variable(&mut self, key: Key) -> Lin {
        let id = self.facts.id(key);
        Lin {
            terms: BTreeMap::from([(id, 1)]),
            constant: 0,
        }
    }

    fn fresh(&mut self, e: &Expr) -> Lin {
        let (lo, hi) = self.range_of(e);
        self.facts.fresh += 1;
        let key = Key::Fresh(self.facts.fresh, lo, hi);
        self.variable(key)
    }

    /// `a + k * b`, where a number that is too large is noted.
    fn combine(&mut self, a: &Lin, k: i128, b: &Lin) -> Lin {
        let mut out = a.clone();
        let mut step = |x: Option<i128>| match x {
            Some(x) => x,
            None => {
                self.overflow = true;
                0
            }
        };
        for (v, n) in &b.terms {
            let sum = step(
                k.checked_mul(*n)
                    .and_then(|m| out.terms.get(v).unwrap_or(&0).checked_add(m)),
            );
            if sum == 0 {
                out.terms.remove(v);
            } else {
                out.terms.insert(*v, sum);
            }
        }
        out.constant = step(
            k.checked_mul(b.constant)
                .and_then(|m| out.constant.checked_add(m)),
        );
        out
    }

    fn scaled(&mut self, k: i128, a: &Lin) -> Lin {
        self.combine(&constant(0), k, a)
    }

    fn show(&self, l: &Lin) -> String {
        let terms: Vec<String> = l.terms.iter().map(|(v, k)| format!("{k}v{v}")).collect();
        format!("{}+{}", terms.join("+"), l.constant)
    }

    /// The sum an expression stands for, or none when it is left out as a
    /// fact because nothing is known of a part of it.
    fn lin(&mut self, e: &Expr) -> Option<Lin> {
        match e {
            Expr::Number {
                magnitude,
                negative,
                ..
            } => {
                let n = i128::try_from(*magnitude).unwrap_or_else(|_| {
                    self.overflow = true;
                    0
                });
                Some(constant(if *negative { -n } else { n }))
            }
            Expr::Name { name, .. } if self.facts.subst.is_some() => {
                self.facts.subst.as_ref()?.get(name).cloned()
            }
            Expr::Name { name, .. } => {
                let at = self.body.names.iter().rposition(|(n, _)| n == name)?;
                let Ty::Int(t) = &self.body.names[at].1 else {
                    return None;
                };
                if self.body.vars.contains(name) {
                    return self.goal.then(|| self.fresh(e));
                }
                if let Some((_, n)) = self.facts.constants.iter().rfind(|(k, _)| *k == at) {
                    return Some(constant(*n));
                }
                Some(self.variable(Key::Name(at, t.min(), t.max())))
            }
            Expr::Field { .. } => match self.path(e) {
                Some(path) => {
                    let (lo, hi) = self.range_of(e);
                    Some(self.variable(Key::Field(path, lo, hi)))
                }
                None => self.goal.then(|| self.fresh(e)),
            },
            Expr::Arith {
                op, left, right, ..
            } => {
                let (l, r) = (self.lin(left), self.lin(right));
                let (l, r) = (l?, r?);
                Some(match op {
                    ArithOp::Add => self.combine(&l, 1, &r),
                    ArithOp::Sub => self.combine(&l, -1, &r),
                    ArithOp::Mul => {
                        if l.terms.is_empty() {
                            self.scaled(l.constant, &r)
                        } else if r.terms.is_empty() {
                            self.scaled(r.constant, &l)
                        } else {
                            let (a, b) = (self.show(&l), self.show(&r));
                            let key = if a <= b {
                                format!("{a}*{b}")
                            } else {
                                format!("{b}*{a}")
                            };
                            self.variable(Key::Product(key))
                        }
                    }
                })
            }
            Expr::Call(call) => {
                let (lo, hi) = match self.types.get(&addr(e)) {
                    Some(Ty::Int(t)) => (t.min(), t.max()),
                    _ => return self.goal.then(|| self.fresh(e)),
                };
                Some(self.variable(Key::Call(addr(call), lo, hi)))
            }
            _ => self.goal.then(|| self.fresh(e)),
        }
    }

    /// A field read from a name that is not a `var`, written out with the
    /// place of the name.
    fn path(&self, e: &Expr) -> Option<String> {
        if self.facts.subst.is_some() {
            return None;
        }
        match e {
            Expr::Name { name, .. } => {
                let at = self.body.names.iter().rposition(|(n, _)| n == name)?;
                (!self.body.vars.contains(name)).then(|| at.to_string())
            }
            Expr::Field { of, name, .. } => Some(format!("{}.{name}", self.path(of)?)),
            _ => None,
        }
    }
}

/// `lin >= 0`.
fn at_least_zero(lin: Lin) -> Formula {
    Formula::Ge(lin)
}

impl<'a> Checker<'a> {
    fn builder<'b>(
        &'b self,
        body: &'b Body<'a>,
        facts: &'b mut Facts,
        goal: bool,
    ) -> Builder<'b, 'a> {
        Builder {
            body,
            types: &self.types,
            facts,
            goal,
            overflow: false,
        }
    }

    /// Whether the numbers a body binds to names have been decided, so that
    /// every whole number has the type it will keep.
    fn decided(body: &Body) -> bool {
        !matches!(body.numbers, super::Numbers::Finding(_))
    }

    /// What `cond` says when it is `holds`, if it is something arith knows:
    /// a comparison of whole numbers, joined with `and`, `or`, and `not`.
    fn said(
        &self,
        body: &Body<'a>,
        facts: &mut Facts,
        cond: &Expr,
        holds: bool,
    ) -> Option<Formula> {
        let both = |a: Option<Formula>, b: Option<Formula>| match (a, b) {
            (Some(a), Some(b)) => Some(Formula::And(vec![a, b])),
            (a, b) => a.or(b),
        };
        let either = |a: Option<Formula>, b: Option<Formula>| match (a, b) {
            (Some(a), Some(b)) => Some(Formula::Or(vec![a, b])),
            _ => None,
        };
        match cond {
            Expr::Name { name, .. }
            | Expr::Case {
                name,
                payload: None,
                ..
            } if name == "true" || name == "false" => {
                let truth = (name == "true") == holds;
                Some(if truth {
                    Formula::And(Vec::new())
                } else {
                    Formula::Or(Vec::new())
                })
            }
            Expr::Match(m) => {
                let side = |arm: usize| match m.arms[arm].body.as_slice() {
                    [Stmt::Expr(e)] => Some(e),
                    _ => None,
                };
                match m.form {
                    Form::Not => self.said(body, facts, &m.of, !holds),
                    Form::And => {
                        let (a, b) = (
                            self.said(body, facts, &m.of, holds),
                            side(0).and_then(|e| self.said(body, facts, e, holds)),
                        );
                        if holds { both(a, b) } else { either(a, b) }
                    }
                    Form::Or => {
                        let (a, b) = (
                            self.said(body, facts, &m.of, holds),
                            side(1).and_then(|e| self.said(body, facts, e, holds)),
                        );
                        if holds { either(a, b) } else { both(a, b) }
                    }
                    Form::Match => self.said_order(body, facts, m, holds),
                    Form::If { .. } => None,
                }
            }
            Expr::Call(call)
                if call.qualifier.as_ref().is_some_and(|(q, _)| q == "Eq")
                    && call.callee == "eq"
                    && call.args.len() == 2 =>
            {
                let (l, r) =
                    self.operands(body, facts, &call.args[0].value, &call.args[1].value)?;
                let (up, down) = (
                    self.sum(body, facts, &l, &r, 0)?,
                    self.sum(body, facts, &r, &l, 0)?,
                );
                if holds {
                    Some(Formula::And(vec![up, down]))
                } else {
                    let (up, down) = (
                        self.sum(body, facts, &l, &r, -1)?,
                        self.sum(body, facts, &r, &l, -1)?,
                    );
                    Some(Formula::Or(vec![up, down]))
                }
            }
            _ => None,
        }
    }

    /// The sums of two whole numbers that are compared.
    fn operands(
        &self,
        body: &Body<'a>,
        facts: &mut Facts,
        a: &Expr,
        b: &Expr,
    ) -> Option<(Lin, Lin)> {
        let int = |e: &Expr| matches!(self.types.get(&addr(e)), Some(Ty::Int(_)));
        if !int(a) || !int(b) {
            return None;
        }
        let mut builder = self.builder(body, facts, false);
        let (l, r) = (builder.lin(a)?, builder.lin(b)?);
        (!builder.overflow).then_some((l, r))
    }

    /// `a - b + extra >= 0`.
    fn sum(
        &self,
        body: &Body<'a>,
        facts: &mut Facts,
        a: &Lin,
        b: &Lin,
        extra: i128,
    ) -> Option<Formula> {
        let mut builder = self.builder(body, facts, false);
        let mut lin = builder.combine(a, -1, b);
        lin.constant = lin.constant.checked_add(extra)?;
        (!builder.overflow).then(|| at_least_zero(lin))
    }

    /// A comparison `a < b`, as the `match` on how `a` and `b` stand that it
    /// is written as: what it says is the cases whose block gives `holds`.
    fn said_order(
        &self,
        body: &Body<'a>,
        facts: &mut Facts,
        m: &crate::parser::Match,
        holds: bool,
    ) -> Option<Formula> {
        let Expr::Call(call) = &m.of else {
            return None;
        };
        if !(call.qualifier.as_ref().is_some_and(|(q, _)| q == "Ord")
            && call.callee == "compare"
            && call.args.len() == 2)
        {
            return None;
        }
        let (l, r) = self.operands(body, facts, &call.args[0].value, &call.args[1].value)?;
        // The cases whose block gives `holds`.
        let (mut less, mut equal, mut greater) = (false, false, false);
        for arm in &m.arms {
            let Pattern::Case { name, .. } = &arm.pattern else {
                return None;
            };
            let [Stmt::Expr(Expr::Name { name: gives, .. } | Expr::Case { name: gives, .. })] =
                arm.body.as_slice()
            else {
                return None;
            };
            if (gives == "true") == holds {
                match name.as_str() {
                    "less" => less = true,
                    "equal" => equal = true,
                    "greater" => greater = true,
                    _ => return None,
                }
            }
        }
        let (r_l, l_r) = (
            self.sum(body, facts, &r, &l, 0)?,
            self.sum(body, facts, &l, &r, 0)?,
        );
        Some(match (less, equal, greater) {
            (false, false, false) => Formula::Or(Vec::new()),
            (true, true, true) => Formula::And(Vec::new()),
            // `l < r` is `r - l - 1 >= 0`, and `l <= r` is `r - l >= 0`.
            (true, false, false) => self.sum(body, facts, &r, &l, -1)?,
            (true, true, false) => r_l,
            (false, false, true) => self.sum(body, facts, &l, &r, -1)?,
            (false, true, true) => l_r,
            (false, true, false) => Formula::And(vec![r_l, l_r]),
            (true, false, true) => Formula::Or(vec![
                self.sum(body, facts, &r, &l, -1)?,
                self.sum(body, facts, &l, &r, -1)?,
            ]),
        })
    }

    /// Notes that `cond` is so, or not so, in the block that follows.
    pub(super) fn assume(&mut self, body: &mut Body<'a>, cond: &Expr, holds: bool) {
        if !Self::decided(body) {
            return;
        }
        let mut facts = std::mem::take(&mut body.facts);
        if let Some(said) = self.said(body, &mut facts, cond, holds) {
            facts.known.push(said);
        }
        body.facts = facts;
    }

    /// Notes what the name defined last was given, when it is a whole number
    /// and not a `var`.
    pub(super) fn assume_binding(&mut self, body: &mut Body<'a>, value: &Expr) {
        let Some((name, Ty::Int(t))) = body.names.last().cloned() else {
            return;
        };
        if !Self::decided(body) || body.vars.contains(&name) {
            return;
        }
        let at = body.names.len() - 1;
        let mut facts = std::mem::take(&mut body.facts);
        let mut builder = self.builder(body, &mut facts, false);
        let given = builder.lin(value);
        let overflow = builder.overflow;
        if let (Some(given), false) = (given, overflow) {
            if given.terms.is_empty() {
                facts.constants.push((at, given.constant));
            }
            let id = facts.id(Key::Name(at, t.min(), t.max()));
            if let Some(same) = self.equal(body, &mut facts, id, &given) {
                facts.known.push(same);
            }
        }
        body.facts = facts;
    }

    /// That variable `id` is `given`, or none when a number grew too large.
    fn equal(&self, body: &Body<'a>, facts: &mut Facts, id: usize, given: &Lin) -> Option<Formula> {
        let me = Lin {
            terms: BTreeMap::from([(id, 1)]),
            constant: 0,
        };
        let mut builder = self.builder(body, facts, false);
        let over = builder.combine(&me, -1, given);
        let under = builder.combine(given, -1, &me);
        (!builder.overflow).then(|| Formula::And(vec![at_least_zero(over), at_least_zero(under)]))
    }

    /// Whether `arith` proves that the result of the operation `e` fits `t`,
    /// and if it does not, the goal it could not prove.
    pub(super) fn fits(&mut self, body: &mut Body<'a>, e: &Expr, t: Int) {
        if !Self::decided(body) || body.contract {
            return;
        }
        let Expr::Arith { op, op_pos, .. } = e else {
            return;
        };
        let mut facts = std::mem::take(&mut body.facts);
        let mut builder = self.builder(body, &mut facts, true);
        let value = builder.lin(e).filter(|_| !builder.overflow);
        // Which side of the range the operation can leave: a sum or a product
        // of numbers that are not negative can only go above it, and a
        // difference of them only below.
        let (above, below) = match (op, t.signed()) {
            (ArithOp::Sub, false) => (false, true),
            (_, false) => (true, false),
            (_, true) => (true, true),
        };
        let mut goals = Vec::new();
        if let Some(value) = &value {
            if above {
                let mut up = value.negated();
                if let Some(up) = &mut up {
                    up.constant = up.constant.saturating_add(t.max());
                    goals.push((format!("<= {}", t.max()), Formula::Ge(up.clone())));
                }
            }
            if below {
                let mut down = value.clone();
                down.constant = down.constant.saturating_sub(t.min());
                goals.push((format!(">= {}", t.min()), Formula::Ge(down)));
            }
        } else {
            goals.push((format!("<= {}", t.max()), Formula::Or(Vec::new())));
        }
        let used = background(&facts, goals.iter().map(|(_, g)| g));
        body.facts = facts;
        for (text, goal) in goals {
            if !proves(&used, &goal) {
                let written = written_expr(e);
                self.errors.push(
                    Error::new(
                        *op_pos,
                        format!(
                            "cannot prove that `{written} {text}`, so `{written}` may not fit `{}`",
                            t.name()
                        ),
                    )
                    .with_fix(format!(
                        "check the operands first, or use `{}`, `{}`, or `{}`",
                        named("wrapping", *op),
                        named("saturating", *op),
                        named("checked", *op)
                    )),
                );
                return;
            }
        }
    }
}

/// What is known, and the range of each value it or the goals are about.
fn background<'g>(facts: &Facts, goals: impl Iterator<Item = &'g Formula>) -> Vec<Formula> {
    let mut used: Vec<Formula> = facts.known.clone();
    let mut ids: Vec<usize> = Vec::new();
    for f in facts.known.iter() {
        collect(f, &mut ids);
    }
    for f in goals {
        collect(f, &mut ids);
    }
    ids.sort_unstable();
    ids.dedup();
    for id in ids {
        if let Some((lo, hi)) = facts.keys[id].range() {
            let me = Lin {
                terms: BTreeMap::from([(id, 1)]),
                constant: 0,
            };
            let mut over = me.clone();
            over.constant = -lo;
            used.push(Formula::Ge(over));
            let mut under = me.negated().expect("small");
            under.constant = hi;
            used.push(Formula::Ge(under));
        }
    }
    used
}

/// The places of the expressions that a block can end with: its last
/// expression, or those the blocks of its last `if` or `match` end with.
pub(super) fn tails(stmts: &[Stmt], out: &mut Vec<usize>) {
    match stmts.last() {
        Some(Stmt::Expr(Expr::Match(m))) if matches!(m.form, Form::If { .. } | Form::Match) => {
            for arm in &m.arms {
                tails(&arm.body, out);
            }
        }
        Some(Stmt::Expr(e)) => out.push(addr(e)),
        _ => {}
    }
}

/// How a contract is written, where `s` gives the text for a name.
fn written_contract(s: &dyn Fn(&str) -> Option<String>, e: &Expr) -> String {
    let group = |inner: &Expr| match inner {
        Expr::Match(m) if matches!(m.form, Form::And | Form::Or) => {
            format!("({})", written_contract(s, inner))
        }
        _ => written_contract(s, inner),
    };
    // A side that is only a name is written as the text it stands for.
    let side = |side: &Expr| match side {
        Expr::Name { name, .. } => s(name).unwrap_or_else(|| name.clone()),
        _ => written_with(s, side),
    };
    match e {
        Expr::Match(m) => match m.form {
            Form::Not => match &m.of {
                Expr::Call(c) if c.callee == "eq" && c.args.len() == 2 => {
                    format!("{} != {}", side(&c.args[0].value), side(&c.args[1].value))
                }
                inner => format!("not {}", group(inner)),
            },
            Form::And => match m.arms[0].body.as_slice() {
                [Stmt::Expr(r)] => format!("{} and {}", group(&m.of), group(r)),
                _ => written_with(s, e),
            },
            Form::Or => match m.arms[1].body.as_slice() {
                [Stmt::Expr(r)] => format!("{} or {}", group(&m.of), group(r)),
                _ => written_with(s, e),
            },
            Form::Match => {
                let Expr::Call(c) = &m.of else {
                    return written_with(s, e);
                };
                let holds = |case: &str| {
                    m.arms.iter().any(|a| {
                        matches!(&a.pattern, Pattern::Case { name, .. } if name == case)
                            && matches!(a.body.as_slice(),
                                [Stmt::Expr(Expr::Name { name, .. })] if name == "true")
                    })
                };
                let op = match (holds("less"), holds("equal"), holds("greater")) {
                    (true, false, false) => "<",
                    (true, true, false) => "<=",
                    (false, false, true) => ">",
                    _ => ">=",
                };
                format!("{} {op} {}", side(&c.args[0].value), side(&c.args[1].value))
            }
            Form::If { .. } => written_with(s, e),
        },
        Expr::Call(c) if c.callee == "eq" && c.args.len() == 2 => {
            format!("{} == {}", side(&c.args[0].value), side(&c.args[1].value))
        }
        _ => written_with(s, e),
    }
}

impl<'a> Checker<'a> {
    /// Checks the contracts of a function at its signature: each is a
    /// comparison of whole numbers, joined with `and`, `or`, and `not`,
    /// over the parameters and, in an `ensures`, the name of the result.
    pub(super) fn contracts(&mut self, f: &'a Function, sig: &'a Signature) {
        if f.requires.is_empty() && f.ensures.is_empty() {
            return;
        }
        let mut body = Body {
            f: Some(f),
            default: None,
            scope: &sig.scope,
            listed: Vec::new(),
            unknown_effect: false,
            missing: Vec::new(),
            names: sig.params.clone(),
            params: sig.params.len(),
            vars: Vec::new(),
            numbers: Numbers::Off,
            decided: HashMap::new(),
            facts: Facts::default(),
            tails: Vec::new(),
            gives: sig.gives.clone(),
            contract: true,
        };
        for e in &f.requires {
            self.contract(&mut body, e);
        }
        let Some((name, pos)) = &f.result_name else {
            if let Some(e) = f.ensures.first() {
                self.errors.push(
                    Error::new(
                        e.pos(),
                        format!(
                            "`{}` has an `ensures`, and its signature does not name its result",
                            f.name
                        ),
                    )
                    .with_fix(
                        "write the result as `-> name: type`, and use the name in the `ensures`",
                    ),
                );
            }
            return;
        };
        if f.ensures.is_empty() {
            return;
        }
        if body.names.iter().any(|(n, _)| n == name) {
            self.errors.push(Error::new(
                *pos,
                format!("`{name}` is already a parameter of `{}`", f.name),
            ));
            return;
        }
        if !matches!(sig.gives, Ty::Int(_)) {
            if !sig.gives.unknown() {
                self.errors.push(Error::new(
                    *pos,
                    "an `ensures` is about a result that is a whole number",
                ));
            }
            return;
        }
        body.names.push((name.clone(), sig.gives.clone()));
        for e in &f.ensures {
            self.contract(&mut body, e);
        }
    }

    /// Checks one contract: it is a `bool`, and is a statement `arith`
    /// knows.
    fn contract(&mut self, body: &mut Body<'a>, e: &Expr) {
        let before = self.errors.len();
        self.condition(body, e);
        if self.errors.len() == before && !self.stated(body, e) {
            self.errors.push(
                Error::new(
                    e.pos(),
                    "a contract is a comparison of whole numbers, joined with `and`, `or`, and `not`",
                )
                .with_fix("write it over the parameters, and the result in an `ensures`"),
            );
        }
    }

    /// Whether `e` is made only of what `arith` knows.
    fn stated(&self, body: &Body<'a>, e: &Expr) -> bool {
        match e {
            Expr::Match(m) => match m.form {
                Form::Not => self.stated(body, &m.of),
                Form::And => {
                    self.stated(body, &m.of)
                        && matches!(m.arms[0].body.as_slice(), [Stmt::Expr(r)] if self.stated(body, r))
                }
                Form::Or => {
                    self.stated(body, &m.of)
                        && matches!(m.arms[1].body.as_slice(), [Stmt::Expr(r)] if self.stated(body, r))
                }
                Form::Match => {
                    let mut facts = Facts::default();
                    self.said(body, &mut facts, e, true).is_some()
                }
                Form::If { .. } => false,
            },
            Expr::Name { name, .. } => name == "true" || name == "false",
            _ => {
                let mut facts = Facts::default();
                matches!(e, Expr::Call(_)) && self.said(body, &mut facts, e, true).is_some()
            }
        }
    }

    /// At a call of a function with contracts, whose arguments are checked:
    /// every `requires` must be proven of the arguments, and every `ensures`
    /// of the arguments and the result is known after.
    pub(super) fn contract_call(
        &mut self,
        body: &mut Body<'a>,
        call: &Call,
        callee: &Function,
        sig: &Signature,
    ) {
        if !Self::decided(body) || (callee.requires.is_empty() && callee.ensures.is_empty()) {
            return;
        }
        let mut facts = std::mem::take(&mut body.facts);
        let mut sums = HashMap::new();
        let mut texts: HashMap<String, String> = HashMap::new();
        for (arg, place) in call.args.iter().zip(params_of(&sig.params, &call.args)) {
            let (name, ty) = &sig.params[place];
            if !matches!(ty, Ty::Int(_)) {
                continue;
            }
            let mut builder = self.builder(body, &mut facts, true);
            let sum = builder.lin(&arg.value).filter(|_| !builder.overflow);
            if let Some(sum) = sum {
                sums.insert(name.clone(), sum);
            }
            let text = written_expr(&arg.value);
            texts.insert(
                name.clone(),
                if text.contains(' ') {
                    format!("({text})")
                } else {
                    text
                },
            );
        }
        facts.subst = Some(sums);
        for requirement in &callee.requires {
            let said = self.said(body, &mut facts, requirement, true);
            let proved = said
                .as_ref()
                .is_some_and(|g| proves(&background(&facts, std::iter::once(g)), g));
            if !proved {
                let goal = written_contract(&|n| texts.get(n).cloned(), requirement);
                self.errors.push(
                    Error::new(
                        call.pos,
                        format!(
                            "cannot prove that `{goal}`, which `{}` requires of its arguments",
                            callee.name
                        ),
                    )
                    .with_fix(
                        "check the arguments first with an `if`, or state it in a `requires`",
                    ),
                );
            }
        }
        if let (Some((name, _)), Ty::Int(t)) = (&callee.result_name, &sig.gives) {
            let id = facts.id(Key::Call(addr(call), t.min(), t.max()));
            let result = Lin {
                terms: BTreeMap::from([(id, 1)]),
                constant: 0,
            };
            facts
                .subst
                .as_mut()
                .expect("set above")
                .insert(name.clone(), result);
            for ensured in &callee.ensures {
                if let Some(said) = self.said(body, &mut facts, ensured, true) {
                    facts.known.push(said);
                }
            }
        }
        facts.subst = None;
        body.facts = facts;
    }

    /// At the end of a body, where `e` is what it gives: every `ensures`
    /// must be proven of it.
    pub(super) fn ensure(&mut self, body: &mut Body<'a>, e: &Expr) {
        let Some(f) = body.f else {
            return;
        };
        let (Some((name, _)), Ty::Int(t)) = (&f.result_name, body.gives.clone()) else {
            return;
        };
        if !Self::decided(body) {
            return;
        }
        let at = body.names.len();
        body.names.push((name.clone(), body.gives.clone()));
        let mut facts = std::mem::take(&mut body.facts);
        let mark = facts.mark();
        let mut builder = self.builder(body, &mut facts, true);
        let value = builder.lin(e).filter(|_| !builder.overflow);
        if let Some(value) = value {
            let id = facts.id(Key::Name(at, t.min(), t.max()));
            if let Some(same) = self.equal(body, &mut facts, id, &value) {
                facts.known.push(same);
            }
        }
        for ensured in &f.ensures {
            let said = self.said(body, &mut facts, ensured, true);
            let proved = said
                .as_ref()
                .is_some_and(|g| proves(&background(&facts, std::iter::once(g)), g));
            if !proved {
                let goal = written_contract(&|_| None, ensured);
                self.errors.push(
                    Error::new(
                        e.pos(),
                        format!(
                            "cannot prove that `{goal}`, which `{}` ensures, where `{name}` is `{}`",
                            f.name,
                            written_expr(e)
                        ),
                    )
                    .with_fix("state what is needed in a `requires`, or check it first with an `if`"),
                );
            }
        }
        facts.reset(mark);
        body.facts = facts;
        body.names.pop();
    }
}

fn named(kind: &str, op: ArithOp) -> String {
    let op = match op {
        ArithOp::Add => "add",
        ArithOp::Sub => "sub",
        ArithOp::Mul => "mul",
    };
    format!("{kind}_{op}")
}

fn collect(f: &Formula, out: &mut Vec<usize>) {
    match f {
        Formula::Ge(l) => out.extend(l.terms.keys().copied()),
        Formula::And(items) | Formula::Or(items) => {
            for item in items {
                collect(item, out);
            }
        }
    }
}
