//! The check of a proof: its steps against the statement the context owes.
//! A step takes none of its own and the kernel opens nothing unasked: a proof
//! that needs the body of a function says where to unfold it.

use std::collections::BTreeMap;

use super::{Failure, Machine, Value};
use crate::core::{Callee, Function, Kind, Operator, Pattern, Proof, Step, Term, Type};

/// Whether the proof proves its statement, or what is wrong with it, in words
/// that say the statement owed and what the step gave.
pub fn check_proof(
    functions: &[Function],
    proofs: &[Proof],
    proof: &Proof,
    limit: u64,
) -> Result<(), String> {
    let mut check = Check {
        functions,
        proofs,
        proof,
        limit,
        facts: Vec::new(),
        known: Vec::new(),
    };
    check.steps(&proof.body, &proof.statement)
}

struct Check<'a> {
    functions: &'a [Function],
    /// Every proof function of the file, which a proof may call.
    proofs: &'a [Proof],
    proof: &'a Proof,
    limit: u64,
    /// What each name in the proof is a proof of.
    facts: Vec<(String, Term)>,
    /// What the cases of the proof so far say: a condition, and whether it held.
    known: Vec<(Term, bool)>,
}

impl Check<'_> {
    fn steps(&mut self, t: &Term, owed: &Term) -> Result<(), String> {
        match &t.kind {
            Kind::Call {
                callee: Callee::Step(Step::Same),
                ..
            } => self.same(owed),
            Kind::Call {
                callee: Callee::Step(Step::Arith),
                args,
            } => self.arith(args, owed),
            Kind::Bind { name, value, rest } => {
                let fact = match &value.kind {
                    Kind::Call {
                        callee: Callee::Function(place),
                        args,
                    } => self.called(*place, args)?,
                    _ => self.unfold(value)?,
                };
                self.facts.push((name.clone(), fact));
                let result = self.steps(rest, owed);
                self.facts.pop();
                result
            }
            // A proof splits into cases, and each block proves the statement
            // knowing which case it is in.
            Kind::Choose { of, arms } => {
                for (pattern, block) in arms {
                    let held = matches!(pattern, Pattern::Case(1, None));
                    self.known.push(((**of).clone(), held));
                    let result = self.steps(block, owed);
                    self.known.pop();
                    result?;
                }
                Ok(())
            }
            _ => Err(format!(
                "a proof of `{}` is owed, and this is not a step the kernel reads yet",
                self.proof.written
            )),
        }
    }

    /// A call of a proof function is a proof of its statement, with the
    /// arguments put for its parameters.
    fn called(&self, place: usize, args: &[Term]) -> Result<Term, String> {
        let proof = self
            .proofs
            .iter()
            .find(|p| p.place == place)
            .ok_or("a call of a proof function the kernel does not read")?;
        let map: Vec<(&str, &Term)> = proof
            .params
            .iter()
            .map(|(n, _)| n.as_str())
            .zip(args)
            .collect();
        Ok(substitute(&proof.statement, &map))
    }

    /// `unfold(f(x))` gives that `f(x)` equals the body of `f` with `x` put in,
    /// where each condition the body tests that the cases say is decided.
    fn unfold(&self, step: &Term) -> Result<Term, String> {
        let Kind::Call {
            callee: Callee::Step(Step::Unfold),
            args,
        } = &step.kind
        else {
            return Err(
                "a name in a proof is given by `unfold`, the only step that does so yet".into(),
            );
        };
        let call = &args[0];
        let Kind::Call {
            callee: Callee::Function(place),
            args: given,
        } = &call.kind
        else {
            return Err("`unfold` is of a call of a function".into());
        };
        let f = self
            .functions
            .iter()
            .find(|f| f.place == *place)
            .ok_or_else(|| {
                "`unfold` of a function that has no core term, so its body is not known".to_string()
            })?;
        if !plain(&f.body) {
            return Err(format!(
                "`unfold` of `{}`, whose body has a loop or a function in it, which the kernel does not read",
                f.name
            ));
        }
        let map: Vec<(&str, &Term)> = f
            .params
            .iter()
            .map(|(n, _)| n.as_str())
            .zip(given)
            .collect();
        let body = substitute(&f.body, &map);
        let body = self.decide(&body)?;
        Ok(equal(call.clone(), body, step.pos))
    }

    /// A body with each `if` whose condition the cases decide as that case.
    fn decide(&self, t: &Term) -> Result<Term, String> {
        if let Kind::Choose { of, arms } = &t.kind {
            let of = self.decide(of)?;
            let seen = self.known.iter().find(|(c, _)| same_shape(c, &of));
            let Some((_, held)) = seen else {
                return Err(format!(
                    "`unfold` needs `{}` to be known to hold or not, and the cases so far do not say",
                    self.render(&of)
                ));
            };
            let case = if *held { 1 } else { 0 };
            let block = arms
                .iter()
                .find(|(p, _)| matches!(p, Pattern::Case(c, None) if *c == case))
                .map(|(_, b)| b)
                .ok_or("an arm is missing")?;
            return self.decide(block);
        }
        let mut out = t.clone();
        out.rebuild(&mut |part| self.decide(part))?;
        Ok(out)
    }

    /// `same` proves a statement with nothing unknown by working it out, and
    /// `a == a` by being what it is. A name the cases say is a number is that
    /// number.
    fn same(&self, owed: &Term) -> Result<(), String> {
        let said: Vec<(&str, Term)> = self
            .known
            .iter()
            .filter(|(_, held)| *held)
            .filter_map(|(c, _)| match &c.kind {
                Kind::Call {
                    callee: Callee::Operator(Operator::Equal),
                    args,
                } => match (&args[0].kind, &args[1].kind) {
                    (Kind::Name(n), Kind::Lit(_)) => Some((n.as_str(), args[1].clone())),
                    (Kind::Lit(_), Kind::Name(n)) => Some((n.as_str(), args[0].clone())),
                    _ => None,
                },
                _ => None,
            })
            .collect();
        let map: Vec<(&str, &Term)> = said.iter().map(|(n, t)| (*n, t)).collect();
        let owed = substitute(owed, &map);
        let unknown = self.unknown(&owed);
        if unknown.is_empty() {
            let mut machine = Machine::new(self.functions, self.limit);
            return match machine.evaluate(&owed, &mut Vec::new()) {
                Ok((Value::Case(1, _), _)) => Ok(()),
                Ok((_, why)) => Err(format!(
                    "`same` cannot prove `{}`, which is false: {}",
                    self.render(&owed),
                    why.map_or(
                        "what it works out to is not `true`".to_string(),
                        |w| format!(
                            "`{}` at {}:{} is {}",
                            w.comparison, w.pos.line, w.pos.col, w.holds
                        )
                    )
                )),
                Err(Failure::Limit(n)) => Err(format!(
                    "`same` did not finish working out `{}` within {n} steps",
                    self.render(&owed)
                )),
                Err(Failure::Stuck(what)) => Err(format!(
                    "`same` cannot work out `{}`: the kernel does not work out {what}",
                    self.render(&owed)
                )),
            };
        }
        if let Kind::Call {
            callee: Callee::Operator(Operator::Equal),
            args,
        } = &owed.kind
            && same_shape(&args[0], &args[1])
        {
            return Ok(());
        }
        Err(format!(
            "`same` proves `a == a` and a statement with nothing unknown, and `{}` has {} unknown",
            self.render(&owed),
            unknown
                .iter()
                .map(|n| format!("`{n}`"))
                .collect::<Vec<_>>()
                .join(", ")
        ))
    }

    /// The parameters a term names, each once.
    fn unknown(&self, t: &Term) -> Vec<String> {
        fn walk(t: &Term, params: &[(String, Type)], out: &mut Vec<String>) {
            if let Kind::Name(n) = &t.kind
                && params.iter().any(|(p, _)| p == n)
                && !out.contains(n)
            {
                out.push(n.clone());
            }
            for part in t.parts() {
                walk(part, params, out);
            }
        }
        let mut out = Vec::new();
        walk(t, &self.proof.params, &mut out);
        out
    }

    /// `arith(p, q)` gives the statement owed if it is an equality that is the
    /// sum of the equalities `p` and `q` say, each times a number, where a
    /// product of unknowns is one unknown of its own.
    fn arith(&self, handed: &[Term], owed: &Term) -> Result<(), String> {
        let mut rows = Vec::new();
        let mut names = Vec::new();
        for arg in handed {
            let Kind::Name(n) = &arg.kind else {
                return Err("`arith` is handed the names of proofs".into());
            };
            let Some((_, fact)) = self.facts.iter().rev().find(|(k, _)| k == n) else {
                return Err(format!(
                    "`{n}` is no proof here, so `arith` cannot be handed it"
                ));
            };
            names.push(format!("`{n}`"));
            rows.push(difference(fact).ok_or_else(|| {
                format!("`arith` reads equalities of whole numbers, and `{n}` is not one")
            })?);
        }
        let goal = difference(owed).ok_or_else(|| {
            format!(
                "`arith` proves an equality of whole numbers so far, and `{}` is not one",
                self.render(owed)
            )
        })?;
        if combination(&rows, &goal) {
            Ok(())
        } else {
            Err(format!(
                "`arith` cannot get `{}` from {}",
                self.render(owed),
                if names.is_empty() {
                    "no proof".to_string()
                } else {
                    names.join(" and ")
                }
            ))
        }
    }

    /// A term as it is written, for a message.
    fn render(&self, t: &Term) -> String {
        let sub = |part: &Term| match &part.kind {
            Kind::Call {
                callee: Callee::Operator(op),
                ..
            } if !matches!(op, Operator::Index | Operator::Size | Operator::Bytes) => {
                format!("({})", self.render(part))
            }
            _ => self.render(part),
        };
        match &t.kind {
            Kind::Lit(crate::core::Lit::Int(n)) => n.to_string(),
            Kind::Name(n) => n.clone(),
            Kind::Call {
                callee: Callee::Operator(op),
                args,
            } if args.len() == 2 && !matches!(op, Operator::Index) => {
                format!("{} {} {}", sub(&args[0]), op.symbol(), sub(&args[1]))
            }
            Kind::Call {
                callee: Callee::Function(place),
                args,
            } => {
                let name = self
                    .functions
                    .iter()
                    .find(|f| f.place == *place)
                    .map_or("f", |f| f.name.as_str());
                let args: Vec<String> = args.iter().map(|a| self.render(a)).collect();
                format!("{name}({})", args.join(", "))
            }
            _ => t.show(),
        }
    }
}

/// Whether a body has only the nodes the kernel puts a name into.
fn plain(t: &Term) -> bool {
    !matches!(
        t.kind,
        Kind::Lambda { .. } | Kind::Loop { .. } | Kind::Apply { .. } | Kind::Bind { .. }
    ) && t.parts().iter().all(|p| plain(p))
}

fn equal(a: Term, b: Term, pos: crate::diag::Pos) -> Term {
    Term::new(
        Kind::Call {
            callee: Callee::Operator(Operator::Equal),
            args: vec![a, b],
        },
        Type::Nothing,
        pos,
    )
}

/// A term with each name in `map` put as the term it stands for.
fn substitute(t: &Term, map: &[(&str, &Term)]) -> Term {
    if let Kind::Name(n) = &t.kind
        && let Some((_, with)) = map.iter().find(|(k, _)| k == n)
    {
        return (*with).clone();
    }
    let mut out = t.clone();
    let _: Result<(), ()> = out.rebuild(&mut |part| Ok(substitute(part, map)));
    out
}

/// Whether two terms are the same, without where each was written.
pub(super) fn same_shape(a: &Term, b: &Term) -> bool {
    key(a) == key(b)
}

/// A term as a string that holds what it is and not where it is written.
fn key(t: &Term) -> String {
    t.show()
}

/// What two sides of an equality differ by, as a sum of products of the
/// things that are not numbers.
fn difference(statement: &Term) -> Option<Poly> {
    let Kind::Call {
        callee: Callee::Operator(Operator::Equal),
        args,
    } = &statement.kind
    else {
        return None;
    };
    let mut d = poly(&args[0])?;
    for (m, c) in poly(&args[1])? {
        let entry = d.entry(m).or_insert(0);
        *entry = entry.checked_sub(c)?;
    }
    d.retain(|_, c| *c != 0);
    Some(d)
}

/// A sum of products: each product is the names of what is multiplied, in order,
/// with a number it is multiplied by.
type Poly = BTreeMap<Vec<String>, i128>;

fn poly(t: &Term) -> Option<Poly> {
    match &t.kind {
        Kind::Lit(crate::core::Lit::Int(n)) => Some(BTreeMap::from([(Vec::new(), *n)])),
        Kind::Call {
            callee: Callee::Operator(op @ (Operator::Add | Operator::Sub)),
            args,
        } => {
            let (mut a, b) = (poly(&args[0])?, poly(&args[1])?);
            for (m, c) in b {
                let entry = a.entry(m).or_insert(0);
                *entry = if *op == Operator::Add {
                    entry.checked_add(c)?
                } else {
                    entry.checked_sub(c)?
                };
            }
            a.retain(|_, c| *c != 0);
            Some(a)
        }
        Kind::Call {
            callee: Callee::Operator(Operator::Mul),
            args,
        } => {
            let (a, b) = (poly(&args[0])?, poly(&args[1])?);
            let mut out: Poly = BTreeMap::new();
            for (ma, ca) in &a {
                for (mb, cb) in &b {
                    let mut m: Vec<String> = ma.iter().chain(mb).cloned().collect();
                    m.sort();
                    let entry = out.entry(m).or_insert(0);
                    *entry = entry.checked_add(ca.checked_mul(*cb)?)?;
                }
            }
            out.retain(|_, c| *c != 0);
            Some(out)
        }
        // A name, a call, or a field is an unknown of its own.
        _ => Some(BTreeMap::from([(vec![key(t)], 1)])),
    }
}

/// Whether `goal` is a sum of the `rows`, each times a number, by solving for
/// the numbers over the fractions.
fn combination(rows: &[Poly], goal: &Poly) -> bool {
    let mut products: Vec<&Vec<String>> = goal.keys().collect();
    for row in rows {
        for m in row.keys() {
            if !products.contains(&m) {
                products.push(m);
            }
        }
    }
    // One equation for each product: the sum of the rows' numbers, each times
    // its number, is the goal's.
    let mut matrix: Vec<Vec<Frac>> = products
        .iter()
        .map(|m| {
            let mut line: Vec<Frac> = rows
                .iter()
                .map(|row| Frac::whole(row.get(*m).copied().unwrap_or(0)))
                .collect();
            line.push(Frac::whole(goal.get(*m).copied().unwrap_or(0)));
            line
        })
        .collect();
    let unknowns = rows.len();
    let mut at = 0;
    for column in 0..unknowns {
        let Some(found) = (at..matrix.len()).find(|&r| !matrix[r][column].is_zero()) else {
            continue;
        };
        matrix.swap(at, found);
        let pivot = matrix[at][column];
        for entry in matrix[at].iter_mut() {
            match entry.over(pivot) {
                Some(f) => *entry = f,
                None => return false,
            }
        }
        let pivot_row = matrix[at].clone();
        for (r, line) in matrix.iter_mut().enumerate() {
            if r == at || line[column].is_zero() {
                continue;
            }
            let factor = line[column];
            for (entry, pivot) in line.iter_mut().zip(&pivot_row) {
                match pivot.times(factor).and_then(|x| entry.minus(x)) {
                    Some(f) => *entry = f,
                    None => return false,
                }
            }
        }
        at += 1;
    }
    // Consistent when no line says nothing is something.
    matrix
        .iter()
        .all(|line| line[..unknowns].iter().any(|f| !f.is_zero()) || line[unknowns].is_zero())
}

/// A fraction of whole numbers, which is what the numbers each row is taken
/// times are.
#[derive(Clone, Copy)]
struct Frac(i128, i128);

impl Frac {
    fn whole(n: i128) -> Frac {
        Frac(n, 1)
    }

    fn is_zero(self) -> bool {
        self.0 == 0
    }

    fn reduced(n: i128, d: i128) -> Option<Frac> {
        let (mut a, mut b) = (n.checked_abs()?, d.checked_abs()?);
        while b != 0 {
            (a, b) = (b, a % b);
        }
        let g = a.max(1);
        let sign = if d < 0 { -1 } else { 1 };
        Some(Frac(n / g * sign, d / g * sign))
    }

    fn times(self, o: Frac) -> Option<Frac> {
        Frac::reduced(self.0.checked_mul(o.0)?, self.1.checked_mul(o.1)?)
    }

    fn over(self, o: Frac) -> Option<Frac> {
        Frac::reduced(self.0.checked_mul(o.1)?, self.1.checked_mul(o.0)?)
    }

    fn minus(self, o: Frac) -> Option<Frac> {
        Frac::reduced(
            self.0
                .checked_mul(o.1)?
                .checked_sub(o.0.checked_mul(self.1)?)?,
            self.1.checked_mul(o.1)?,
        )
    }
}
