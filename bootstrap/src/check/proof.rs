//! What `arith` is given, and what it is asked, when a plain `+`, `-`, or `*`
//! on whole numbers is checked.
//!
//! The facts in scope are the condition of each `if` the operation is inside,
//! which holds in the first block and is false in the `else`, and what each
//! binding that is not a `var` was given. The range of each value's type and
//! the value of each literal are added when a goal is asked. A `var` can be
//! given another value between a fact and a use, so nothing is known of one
//! but the range of its type, except inside one round of a loop that assigns
//! it, where the value it holds is tracked as a sum of the variables.

use std::collections::{BTreeMap, HashMap, HashSet};

use super::arith::{Formula, Lin, proves};
use super::{
    Body, Checker, Int, Numbers, Signature, Ty, addr, params_of, pattern_names, statements,
    written_expr, written_with,
};
use crate::diag::{Error, Pos};
use crate::parser::{ArithOp, Call, Expr, Form, Function, Loop, Pattern, Range, Stmt};

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

/// A function of `Bits` whose result `arith` knows something of.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub(super) enum BitOp {
    And,
    Or,
    Xor,
    ShiftLeft,
    ShiftRight,
}

/// A call of a function of `Bits` on a whole number of type `ty`, with the
/// places among its arguments of the two numbers it works on: the number and
/// the other number, or the amount.
#[derive(Clone, Copy, Debug)]
pub(super) struct BitCall {
    pub(super) op: BitOp,
    pub(super) ty: Int,
    pub(super) first: usize,
    pub(super) second: usize,
}

/// What `arith` has been told in a body, and the variables it has.
#[derive(Default)]
pub(super) struct Facts {
    /// The calls of the functions of `Bits` that have been checked, by the
    /// place of the call.
    bits: HashMap<usize, BitCall>,
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
    /// The `var`s, by their place among the names, whose value is tracked
    /// through one round of a loop, each with the sum it holds now or none
    /// when it is not known. The newest entry for a place is the one that
    /// counts.
    held: Vec<(usize, Option<Lin>)>,
    /// The version of each `var`, by its place among the names, that the
    /// facts about its fields are about. A version is never used twice.
    versions: HashMap<usize, usize>,
    epoch: usize,
}

/// How much is known at a moment, to go back to when a block ends.
pub(super) struct Mark {
    known: usize,
    constants: usize,
    held: usize,
    versions: HashMap<usize, usize>,
}

impl Facts {
    /// How much is known now, to go back to when a block ends.
    pub(super) fn mark(&self) -> Mark {
        Mark {
            known: self.known.len(),
            constants: self.constants.len(),
            held: self.held.len(),
            versions: self.versions.clone(),
        }
    }

    /// Forgets what was learned after the mark.
    pub(super) fn reset(&mut self, mark: &Mark) {
        self.known.truncate(mark.known);
        self.constants.truncate(mark.constants);
        self.held.truncate(mark.held);
        self.versions.clone_from(&mark.versions);
    }

    /// The `var` at `at` may hold another value: nothing known of its
    /// fields so far is known of what it holds now.
    fn bump(&mut self, at: usize) {
        self.epoch += 1;
        self.versions.insert(at, self.epoch);
    }

    /// Notes that `call`, a call of the function `name` of `Bits` on a whole
    /// number of type `ty`, is one whose result `arith` knows something of.
    pub(super) fn note_bit_call(&mut self, call: &Call, sig: &Signature, name: &str, ty: Int) {
        let op = match name {
            "bit_and" => BitOp::And,
            "bit_or" => BitOp::Or,
            "bit_xor" => BitOp::Xor,
            "shift_left" => BitOp::ShiftLeft,
            "shift_right" => BitOp::ShiftRight,
            _ => return,
        };
        let places = params_of(&sig.params, &call.args);
        let (Some(first), Some(second)) = (
            places.iter().position(|p| *p == 0),
            places.iter().position(|p| *p == 1),
        ) else {
            return;
        };
        let info = BitCall {
            op,
            ty,
            first,
            second,
        };
        self.bits.insert(addr(call), info);
    }

    /// What the `var` at `at` holds, when it is tracked: the sum, or none
    /// when it is not known.
    fn holds(&self, at: usize) -> Option<&Option<Lin>> {
        self.held.iter().rfind(|(k, _)| *k == at).map(|(_, v)| v)
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
    /// The calls of `narrow`, which give the number they are given.
    narrowed: &'b HashSet<usize>,
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
                    if let Some(Some(held)) = self.facts.holds(at) {
                        return Some(held.clone());
                    }
                    return self.goal.then(|| self.fresh(e));
                }
                if let Some((_, n)) = self.facts.constants.iter().rfind(|(k, _)| *k == at) {
                    return Some(constant(*n));
                }
                Some(self.variable(Key::Name(at, t.min(), t.max())))
            }
            Expr::Field { of, name, .. } if name == "size" && self.facts.subst.is_some() => {
                let Expr::Name { name: list, .. } = &**of else {
                    return None;
                };
                self.facts
                    .subst
                    .as_ref()?
                    .get(&format!("{list}.size"))
                    .cloned()
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
            Expr::Call(call) if self.narrowed.contains(&addr(call)) => {
                self.lin(&call.args[0].value)
            }
            Expr::Call(call)
                if self.facts.subst.is_none() && self.facts.bits.contains_key(&addr(call)) =>
            {
                self.bit_call(call)
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

    /// Whether `goal` follows from what is known.
    fn provable(&mut self, goal: &Formula) -> bool {
        let used = background(self.facts, std::iter::once(goal));
        proves(&used, goal)
    }

    /// Says that `lin >= 0`, which is so whatever values the call is given.
    fn state(&mut self, lin: Lin) {
        let fact = Formula::Ge(lin);
        if !self.facts.known.contains(&fact) {
            self.facts.known.push(fact);
        }
    }

    /// The sum a call of a function of `Bits` stands for: the number it
    /// gives, which of an unsigned number is known by what is known of its
    /// operands, where that is true of every value.
    ///
    /// - `bit_and(a, b)` is at most `a` and at most `b`.
    /// - `bit_or(a, b)` and `bit_xor(a, b)` are below the least power of two
    ///   that is above both `a` and `b`.
    /// - `shift_right(a, k)` by a number `k` is `a / 2^k`, rounded down.
    /// - `shift_left(a, k)` by a number `k` is `a * 2^k` when that is proven
    ///   to fit the type, since no bit is dropped.
    fn bit_call(&mut self, call: &Call) -> Option<Lin> {
        let info = self.facts.bits[&addr(call)];
        let t = info.ty;
        let result = {
            let key = Key::Call(addr(call), t.min(), t.max());
            self.variable(key)
        };
        let operands = (
            self.lin(&call.args[info.first].value),
            self.lin(&call.args[info.second].value),
        );
        let (Some(a), Some(b)) = operands else {
            return Some(result);
        };
        // A number too large for the facts is no fact, and no overflow of
        // the operands.
        let base = self.overflow;
        match info.op {
            BitOp::And if !t.signed() => {
                for side in [&a, &b] {
                    let up = self.combine(side, -1, &result);
                    if self.overflow == base {
                        self.state(up);
                    }
                }
            }
            BitOp::Or | BitOp::Xor if !t.signed() => {
                // The least number of bits that both operands are proven to
                // fit, found by halving, as what fits more bits fits fewer.
                let (mut low, mut high) = (0u32, t.bits());
                while low < high {
                    let mid = low + (high - low) / 2;
                    let top = (1i128 << mid) - 1;
                    let both = Formula::And(vec![
                        Formula::Ge(self.combine(&constant(top), -1, &a)),
                        Formula::Ge(self.combine(&constant(top), -1, &b)),
                    ]);
                    if self.overflow != base {
                        self.overflow = base;
                        return Some(result);
                    }
                    if self.provable(&both) {
                        high = mid;
                    } else {
                        low = mid + 1;
                    }
                }
                if low < t.bits() {
                    let top = constant((1i128 << low) - 1);
                    let up = self.combine(&top, -1, &result);
                    if self.overflow == base {
                        self.state(up);
                    }
                }
            }
            BitOp::ShiftRight if !t.signed() => {
                if let Some(power) = self.power(&b, t) {
                    // `power * result <= a <= power * result + power - 1`.
                    let scaled = self.scaled(power, &result);
                    let low = self.combine(&a, -1, &scaled);
                    let mut high = self.combine(&scaled, -1, &a);
                    high.constant = high.constant.saturating_add(power - 1);
                    if self.overflow == base {
                        self.state(low);
                        self.state(high);
                    }
                }
            }
            BitOp::ShiftLeft => {
                if let Some(power) = self.power(&b, t) {
                    let product = self.scaled(power, &a);
                    let (mut up, down) = (
                        product.negated().unwrap_or_default(),
                        self.combine(&product, 1, &constant(-t.min())),
                    );
                    up.constant = up.constant.saturating_add(t.max());
                    let fits = Formula::And(vec![Formula::Ge(up), Formula::Ge(down)]);
                    if self.overflow == base && self.provable(&fits) {
                        return Some(product);
                    }
                }
            }
            _ => {}
        }
        self.overflow = base;
        Some(result)
    }

    /// Two to the power of the amount `by` of a shift of a number of type
    /// `t`, when the amount is a number below the bits of `t`.
    fn power(&self, by: &Lin, t: Int) -> Option<i128> {
        let k = u32::try_from(by.constant).ok().filter(|k| *k < t.bits())?;
        by.terms.is_empty().then(|| 1i128 << k)
    }

    /// The sum the size of the list `of` stands for.
    fn list_size(&mut self, of: &Expr) -> Option<Lin> {
        let range = (0, u64::MAX as i128);
        if self.facts.subst.is_some() {
            return None;
        }
        match self.path(of) {
            Some(path) => Some(self.variable(Key::Field(format!("{path}.size"), range.0, range.1))),
            None => self.goal.then(|| {
                self.facts.fresh += 1;
                let key = Key::Fresh(self.facts.fresh, range.0, range.1);
                self.variable(key)
            }),
        }
    }

    /// A field read from a name, written out with the place of the name, and
    /// for a `var` its version.
    fn path(&self, e: &Expr) -> Option<String> {
        if self.facts.subst.is_some() {
            return None;
        }
        match e {
            Expr::Name { name, .. } => {
                let at = self.body.names.iter().rposition(|(n, _)| n == name)?;
                Some(root(self.body, self.facts, at))
            }
            Expr::Field { of, name, .. } => Some(format!("{}.{name}", self.path(of)?)),
            _ => None,
        }
    }
}

/// How the name at `at` starts the path of a field: its place, and for a
/// `var` the version of what it holds.
fn root(body: &Body, facts: &Facts, at: usize) -> String {
    if body.vars.contains(&body.names[at].0) {
        format!("{at}@{}", facts.versions.get(&at).copied().unwrap_or(0))
    } else {
        at.to_string()
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
            narrowed: &self.narrowed,
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
                    Form::If { .. } | Form::Try => None,
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
        if let Some((_, Ty::List(_))) = body.names.last() {
            if Self::decided(body) {
                self.give_list(body, body.names.len() - 1, value);
            }
            return;
        }
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

    /// Notes the size of the list the name at `at` was given, when the size
    /// is known: that of a list written out, or of another name's list. A
    /// `var` is given a version of its own first.
    fn give_list(&mut self, body: &mut Body<'a>, at: usize, value: &Expr) {
        let mut facts = std::mem::take(&mut body.facts);
        let range = (0, u64::MAX as i128);
        let mut builder = self.builder(body, &mut facts, false);
        let given = match value {
            Expr::List { items, .. } => Some(constant(items.len() as i128)),
            Expr::Name { .. } => builder.list_size(value),
            _ => None,
        };
        let overflow = builder.overflow;
        if body.vars.contains(&body.names[at].0) {
            facts.bump(at);
        }
        let id = facts.id(Key::Field(
            format!("{}.size", root(body, &facts, at)),
            range.0,
            range.1,
        ));
        if let (Some(given), false) = (given, overflow)
            && let Some(same) = self.equal(body, &mut facts, id, &given)
        {
            facts.known.push(same);
        }
        body.facts = facts;
    }

    /// Notes that the name defined last, the number of a `loop` over the
    /// range, is not below the first end and is below the second.
    pub(super) fn assume_range(&mut self, body: &mut Body<'a>, r: &Range) {
        let Some((_, Ty::Int(t))) = body.names.last().cloned() else {
            return;
        };
        if !Self::decided(body) {
            return;
        }
        let at = body.names.len() - 1;
        let mut facts = std::mem::take(&mut body.facts);
        let id = facts.id(Key::Name(at, t.min(), t.max()));
        let me = Lin {
            terms: BTreeMap::from([(id, 1)]),
            constant: 0,
        };
        let (from, to) = {
            let mut builder = self.builder(body, &mut facts, false);
            let (from, to) = (builder.lin(&r.from), builder.lin(&r.to));
            if builder.overflow {
                (None, None)
            } else {
                (from, to)
            }
        };
        // `i >= from`, and `i < to`, which is `to - i - 1 >= 0`.
        if let Some(from) = from
            && let Some(said) = self.sum(body, &mut facts, &me, &from, 0)
        {
            facts.known.push(said);
        }
        if let Some(to) = to
            && let Some(said) = self.sum(body, &mut facts, &to, &me, -1)
        {
            facts.known.push(said);
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

    /// Whether `arith` proves that `index` is below the size of the list
    /// `of`, and if it does not, the goal it could not prove.
    pub(super) fn in_range(&mut self, body: &mut Body<'a>, of: &Expr, index: &Expr, pos: Pos) {
        if !Self::decided(body) || body.contract {
            return;
        }
        let mut facts = std::mem::take(&mut body.facts);
        let mut builder = self.builder(body, &mut facts, true);
        let (at, size) = (builder.lin(index), builder.list_size(of));
        let overflow = builder.overflow;
        let goal = match (at, size) {
            (Some(at), Some(size)) if !overflow => self.sum(body, &mut facts, &size, &at, -1),
            _ => None,
        };
        let goals: Vec<&Formula> = goal.iter().collect();
        let used = background(&facts, goals.into_iter());
        body.facts = facts;
        if !goal.as_ref().is_some_and(|g| proves(&used, g)) {
            let (i, xs) = (written_expr(index), written_expr(of));
            self.errors.push(
                Error::new(
                    pos,
                    format!("cannot prove that `{i} < {xs}.size`, which indexing `{xs}` needs"),
                )
                .with_fix(format!(
                    "put `{i}` in the range of a `loop` over `0..{xs}.size`, check `{i} < {xs}.size` \
                     with an `if`, or state it in a `requires`"
                )),
            );
        }
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
                    .with_fix(match t {
                        // A count has no wrapping, saturating, or checked arithmetic.
                        Int::Nat => "check the operands first".to_string(),
                        _ => format!(
                            "check the operands first, or use `{}`, `{}`, or `{}`",
                            named("wrapping", *op),
                            named("saturating", *op),
                            named("checked", *op)
                        ),
                    }),
                );
                return;
            }
        }
    }
}

impl<'a> Checker<'a> {
    /// Whether `arith` proves that the amount `by` of a shift of a number of
    /// type `t` is below the number of bits of `t`, and if it does not, the
    /// goal it could not prove.
    pub(super) fn shifts(&mut self, body: &mut Body<'a>, by: &Expr, t: Int, pos: Pos) {
        if !Self::decided(body) || body.contract {
            return;
        }
        let mut facts = std::mem::take(&mut body.facts);
        let mut builder = self.builder(body, &mut facts, true);
        let value = builder.lin(by).filter(|_| !builder.overflow);
        let goal = value.and_then(|value| {
            let mut up = value.negated()?;
            up.constant = up.constant.checked_add(i128::from(t.bits()) - 1)?;
            Some(Formula::Ge(up))
        });
        let used = background(&facts, goal.iter());
        body.facts = facts;
        if !goal.as_ref().is_some_and(|g| proves(&used, g)) {
            let by = written_expr(by);
            let bits = t.bits();
            self.errors.push(
                Error::new(
                    pos,
                    format!(
                        "cannot prove that `{by} < {bits}`, which a shift of a `{}` needs",
                        t.name()
                    ),
                )
                .with_fix(format!(
                    "check `{by} < {bits}` with an `if`, or state it in a `requires`"
                )),
            );
        }
    }

    /// Whether `arith` proves that `value`, a whole number of type `from`,
    /// fits `to`, and if it does not, the goal it could not prove. `written`
    /// is the call, as it is written.
    pub(super) fn narrows(
        &mut self,
        body: &mut Body<'a>,
        value: &Expr,
        (from, to): (Int, Int),
        (written, pos): (&str, Pos),
    ) {
        if !Self::decided(body) || body.contract {
            return;
        }
        let mut facts = std::mem::take(&mut body.facts);
        let mut builder = self.builder(body, &mut facts, true);
        let lin = builder.lin(value).filter(|_| !builder.overflow);
        // The sides of the range of `to` that a value of `from` can leave.
        let mut goals = Vec::new();
        if from.max() > to.max() {
            let up = lin.as_ref().and_then(|l| {
                let mut up = l.negated()?;
                up.constant = up.constant.checked_add(to.max())?;
                Some(Formula::Ge(up))
            });
            goals.push((format!("<= {}", to.max()), up));
        }
        if from.min() < to.min() {
            let down = lin.as_ref().and_then(|l| {
                let mut down = l.clone();
                down.constant = down.constant.checked_sub(to.min())?;
                Some(Formula::Ge(down))
            });
            goals.push((format!(">= {}", to.min()), down));
        }
        let used = background(&facts, goals.iter().filter_map(|(_, g)| g.as_ref()));
        body.facts = facts;
        let value = written_expr(value);
        for (text, goal) in goals {
            if !goal.as_ref().is_some_and(|g| proves(&used, g)) {
                self.errors.push(
                    Error::new(
                        pos,
                        format!(
                            "cannot prove that `{value} {text}`, so `{written}` may not fit `{}`",
                            to.name()
                        ),
                    )
                    .with_fix(
                        "check the value first, state it in a `requires`, or use \
                         `wrapping_narrow`, `saturating_narrow`, or `checked_narrow`",
                    ),
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
        Some(Stmt::Expr(Expr::Return { .. })) => {}
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
            Form::If { .. } | Form::Try => written_with(s, e),
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
            loops: 0,
            outer_loops: 0,
            returns: Ty::Nothing,
            returned: None,
            lambdas: 0,
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
                Form::If { .. } | Form::Try => false,
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
            if matches!(ty, Ty::List(_)) {
                let mut builder = self.builder(body, &mut facts, true);
                if let Some(size) = builder.list_size(&arg.value).filter(|_| !builder.overflow) {
                    sums.insert(format!("{name}.size"), size);
                }
                texts.insert(name.clone(), written_expr(&arg.value));
                continue;
            }
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
        facts.reset(&mark);
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

/// What one way through an `if` or a `match` leaves known.
pub(super) struct Path {
    facts: Vec<Formula>,
    held: Vec<(usize, Option<Lin>)>,
}

/// Whether a block never reaches its end, because it ends with a `break` or
/// a `return`, or with an `if` with an `else`, or a `match`, each of whose
/// blocks does.
pub(super) fn ends_in_exit(stmts: &[Stmt]) -> bool {
    match stmts.last() {
        Some(Stmt::Expr(Expr::Break { .. } | Expr::Return { .. })) => true,
        Some(Stmt::Expr(Expr::Match(m))) => match m.form {
            Form::If { otherwise } => otherwise && m.arms.iter().all(|a| ends_in_exit(&a.body)),
            Form::Match => m.arms.iter().all(|a| ends_in_exit(&a.body)),
            _ => false,
        },
        _ => false,
    }
}

/// Whether the variable `id` stands for a value of a name, or of a field of
/// one, that is defined at or after the place `outer` among the names, and is
/// gone once the block it is in ends.
fn is_local(keys: &[Key], id: usize, outer: usize) -> bool {
    match &keys[id] {
        Key::Name(at, ..) => *at >= outer,
        Key::Field(path, ..) => path
            .split(['.', '@'])
            .next()
            .and_then(|a| a.parse::<usize>().ok())
            .is_none_or(|a| a >= outer),
        Key::Product(text) => {
            let mut rest = text.as_str();
            while let Some(at) = rest.find('v') {
                rest = &rest[at + 1..];
                let digits: String = rest.chars().take_while(char::is_ascii_digit).collect();
                if let Ok(inner) = digits.parse::<usize>()
                    && inner != id
                    && is_local(keys, inner, outer)
                {
                    return true;
                }
            }
            false
        }
        Key::Fresh(..) | Key::Call(..) => false,
    }
}

impl<'a> Checker<'a> {
    /// The `var`s, by their place among the names, that the blocks assign,
    /// however deep.
    pub(super) fn assigned_vars(&self, body: &Body<'a>, blocks: &[&[Stmt]]) -> Vec<usize> {
        let mut names: Vec<&str> = Vec::new();
        for block in blocks {
            for stmt in statements(block) {
                match stmt {
                    Stmt::Define { name, .. } | Stmt::Assign { name, .. } => names.push(name),
                    Stmt::Destructure { pattern, .. } => {
                        names.extend(pattern_names(pattern).into_iter().map(|(n, _)| n));
                    }
                    Stmt::Expr(_) => {}
                }
            }
        }
        let mut places: Vec<usize> = names
            .into_iter()
            .filter(|n| body.vars.iter().any(|v| v == n))
            .filter_map(|n| body.names.iter().rposition(|(m, _)| m == n))
            .collect();
        places.sort_unstable();
        places.dedup();
        places
    }

    /// A round of a loop begins: each `var` the loop assigns holds what it
    /// held when the round began, which is nothing but the range of its type.
    pub(super) fn begin_round(&mut self, body: &mut Body<'a>, assigned: &[usize]) {
        if !Self::decided(body) {
            return;
        }
        for &at in assigned {
            body.facts.bump(at);
            let Ty::Int(t) = &body.names[at].1 else {
                continue;
            };
            body.facts.fresh += 1;
            let key = Key::Fresh(body.facts.fresh, t.min(), t.max());
            let id = body.facts.id(key);
            let held = Lin {
                terms: BTreeMap::from([(id, 1)]),
                constant: 0,
            };
            body.facts.held.push((at, Some(held)));
        }
    }

    /// The `var`s that are tracked, among `assigned`, are no longer known.
    pub(super) fn forget(&mut self, body: &mut Body<'a>, assigned: &[usize]) {
        for &at in assigned {
            body.facts.bump(at);
            if body.facts.holds(at).is_some() {
                body.facts.held.push((at, None));
            }
        }
    }

    /// Every `var` is no longer known.
    pub(super) fn forget_all(&mut self, body: &mut Body<'a>) {
        let mut tracked: Vec<usize> = body.facts.held.iter().map(|(at, _)| *at).collect();
        tracked.extend(
            body.vars
                .iter()
                .filter_map(|v| body.names.iter().rposition(|(n, _)| n == v)),
        );
        tracked.sort_unstable();
        tracked.dedup();
        self.forget(body, &tracked);
    }

    /// The sum `value` stands for, with every part it knows nothing of a
    /// variable of its own, or none when a number grew too large.
    fn value_of(&mut self, body: &mut Body<'a>, value: &Expr) -> Option<Lin> {
        let mut facts = std::mem::take(&mut body.facts);
        let mut builder = self.builder(body, &mut facts, true);
        let sum = builder.lin(value).filter(|_| !builder.overflow);
        body.facts = facts;
        sum
    }

    /// The `var` at `at`, tracked, is given `value`.
    pub(super) fn assign(&mut self, body: &mut Body<'a>, at: usize, value: &Expr) {
        if !Self::decided(body) {
            return;
        }
        match body.names[at].1 {
            Ty::Int(_) if body.facts.holds(at).is_some() => {
                let sum = self.value_of(body, value);
                body.facts.held.push((at, sum));
            }
            Ty::Int(_) => {}
            Ty::List(_) => self.give_list(body, at, value),
            _ => body.facts.bump(at),
        }
    }

    /// A field of the `var` at `at` is given a value.
    pub(super) fn assign_field(&mut self, body: &mut Body<'a>, at: usize) {
        if Self::decided(body) {
            body.facts.bump(at);
        }
    }

    /// The `var` defined last is given `value`.
    pub(super) fn track_var(&mut self, body: &mut Body<'a>, value: &Expr) {
        if !Self::decided(body) || body.contract {
            return;
        }
        let at = body.names.len() - 1;
        match body.names[at].1 {
            Ty::Int(_) => {}
            Ty::List(_) => return self.give_list(body, at, value),
            _ => return body.facts.bump(at),
        }
        let sum = self.value_of(body, value);
        body.facts.held.push((at, sum));
    }

    /// What an arm of an `if` or a `match` leaves known at its end, where
    /// `mark` is how much was known before it, and `assigned` is what the
    /// arms assign.
    pub(super) fn leave_arm(&self, body: &Body<'a>, mark: &Mark, assigned: &[usize]) -> Path {
        Path {
            facts: body.facts.known[mark.known..].to_vec(),
            held: assigned
                .iter()
                .filter_map(|&at| body.facts.holds(at).map(|v| (at, v.clone())))
                .collect(),
        }
    }

    /// After an `if` or a `match`, whose arms that reach their end left the
    /// `paths`, and whose names began at `outer`: what is so is what is so of
    /// one of the paths. `excluded` is whether an arm never reaches its end.
    pub(super) fn join(
        &mut self,
        body: &mut Body<'a>,
        outer: usize,
        paths: Vec<Path>,
        assigned: &[usize],
        excluded: bool,
    ) {
        if !Self::decided(body) {
            return;
        }
        for &at in assigned {
            body.facts.bump(at);
        }
        if paths.is_empty() {
            return;
        }
        let mut facts = std::mem::take(&mut body.facts);
        let mut ands: Vec<Vec<Formula>> = paths.iter().map(|p| p.facts.clone()).collect();
        let mut changed = excluded;
        for &at in assigned {
            let Some(before) = facts.holds(at).cloned() else {
                continue;
            };
            let Ty::Int(t) = &body.names[at].1 else {
                continue;
            };
            let values: Vec<Option<Lin>> = paths
                .iter()
                .map(
                    |p| match p.held.iter().find(|(k, _)| *k == at).map(|(_, v)| v) {
                        Some(v) => v.clone(),
                        None => before.clone(),
                    },
                )
                .collect();
            if values.iter().all(|v| *v == before) {
                continue;
            }
            changed = true;
            if values.iter().any(Option::is_none) {
                facts.held.push((at, None));
                continue;
            }
            facts.fresh += 1;
            let id = facts.id(Key::Fresh(facts.fresh, t.min(), t.max()));
            for (and, value) in ands.iter_mut().zip(&values) {
                let value = value.as_ref().expect("every path gives a sum");
                if let Some(same) = self.equal(body, &mut facts, id, value) {
                    and.push(same);
                }
            }
            facts.held.push((
                at,
                Some(Lin {
                    terms: BTreeMap::from([(id, 1)]),
                    constant: 0,
                }),
            ));
        }
        if changed {
            for and in &mut ands {
                and.retain(|f| {
                    let mut ids = Vec::new();
                    collect(f, &mut ids);
                    !ids.iter().any(|&i| is_local(&facts.keys, i, outer))
                });
            }
            if !ands.iter().any(Vec::is_empty) {
                let mut sides: Vec<Formula> = ands.into_iter().map(Formula::And).collect();
                facts.known.push(if sides.len() == 1 {
                    sides.remove(0)
                } else {
                    Formula::Or(sides)
                });
            }
        }
        body.facts = facts;
    }

    /// The sum of the measure of a loop, now.
    pub(super) fn measure(&mut self, body: &mut Body<'a>, e: &Expr) -> Option<Lin> {
        if !Self::decided(body) || body.contract {
            return None;
        }
        self.value_of(body, e)
    }

    /// At the end of a round of a loop that has the measure `e`, which was
    /// `start` when the round began: `arith` must prove that `e` is smaller,
    /// and is still a number of an unsigned type.
    pub(super) fn decreased(&mut self, body: &mut Body<'a>, l: &Loop, e: &Expr, start: &Lin) {
        if !Self::decided(body) || body.contract || ends_in_exit(&l.body) {
            return;
        }
        let end = self.value_of(body, e);
        let mut facts = std::mem::take(&mut body.facts);
        let mut goals = Vec::new();
        if let Some(end) = &end {
            let mut builder = self.builder(body, &mut facts, true);
            let mut less = builder.combine(start, -1, end);
            less.constant = less.constant.saturating_sub(1);
            if !builder.overflow {
                goals.push((
                    format!("`{}` is smaller than when the round began", written_expr(e)),
                    Formula::Ge(less),
                ));
                goals.push((
                    format!(
                        "`{}` is not below zero where the round ends",
                        written_expr(e)
                    ),
                    Formula::Ge(end.clone()),
                ));
            }
        }
        let used = background(&facts, goals.iter().map(|(_, g)| g));
        body.facts = facts;
        let mut failed = goals
            .is_empty()
            .then(|| format!("`{}` is smaller than when the round began", written_expr(e)));
        for (text, goal) in &goals {
            if failed.is_none() && !proves(&used, goal) {
                failed = Some(text.clone());
            }
        }
        if let Some(goal) = failed {
            self.errors.push(
                Error::new(
                    l.end,
                    format!("cannot prove that {goal}, so this `loop` may not end"),
                )
                .with_fix(
                    "make each round that goes on make the measure smaller, or check what it \
                     needs first with an `if`",
                ),
            );
        }
    }
}
