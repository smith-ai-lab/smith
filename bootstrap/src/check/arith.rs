//! `arith`, the procedure that decides linear arithmetic over the integers.
//!
//! A fact or a goal is a formula whose atoms say that a sum of variables, each
//! times a whole number, plus a whole number, is at least zero, joined with
//! `and` and `or`. A goal is proven when no values of the variables make the
//! facts true and the goal false, which is decided exactly, by the Omega test.
//! It never looks at the size of a variable's range, so the time it takes
//! depends on the formulas alone.
//!
//! When a number grows beyond what 128 bits hold, the goal is not proven.

use std::collections::BTreeMap;

/// A sum of variables, each times a whole number, plus a whole number.
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Lin {
    pub terms: BTreeMap<usize, i128>,
    pub constant: i128,
}

/// What a fact or a goal says.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Formula {
    /// The sum is at least zero.
    Ge(Lin),
    /// Every one holds. `And(vec![])` is true.
    And(Vec<Formula>),
    /// One holds at least. `Or(vec![])` is false.
    Or(Vec<Formula>),
}

impl Formula {
    /// What a formula says is not so, or none when a number is too large.
    pub fn not(&self) -> Option<Formula> {
        Some(match self {
            Formula::Ge(l) => {
                let mut l = l.negated()?;
                l.constant = l.constant.checked_sub(1)?;
                Formula::Ge(l)
            }
            Formula::And(items) => {
                Formula::Or(items.iter().map(Formula::not).collect::<Option<_>>()?)
            }
            Formula::Or(items) => {
                Formula::And(items.iter().map(Formula::not).collect::<Option<_>>()?)
            }
        })
    }
}

impl Lin {
    /// A sum with every part negated, or none when a number is too large.
    pub fn negated(&self) -> Option<Lin> {
        let mut out = Lin {
            terms: BTreeMap::new(),
            constant: self.constant.checked_neg()?,
        };
        for (v, k) in &self.terms {
            out.terms.insert(*v, k.checked_neg()?);
        }
        Some(out)
    }
}

/// Whether the goal follows from the facts: no values of the variables make
/// every fact true and the goal false.
pub fn proves(facts: &[Formula], goal: &Formula) -> bool {
    let Some(denied) = goal.not() else {
        return false;
    };
    let mut all: Vec<&Formula> = facts.iter().collect();
    all.push(&denied);
    matches!(satisfiable(Vec::new(), all), Ok(false))
}

/// A number grew past 128 bits.
struct Overflow;

type Res<T> = Result<T, Overflow>;

fn add(a: i128, b: i128) -> Res<i128> {
    a.checked_add(b).ok_or(Overflow)
}

fn sub(a: i128, b: i128) -> Res<i128> {
    a.checked_sub(b).ok_or(Overflow)
}

fn mul(a: i128, b: i128) -> Res<i128> {
    a.checked_mul(b).ok_or(Overflow)
}

fn gcd(a: i128, b: i128) -> i128 {
    let (mut a, mut b) = (a.unsigned_abs(), b.unsigned_abs());
    while b != 0 {
        (a, b) = (b, a % b);
    }
    a as i128
}

/// A constraint: the sum of each variable times its number, plus `c`, is at
/// least zero.
#[derive(Clone, Debug)]
struct Con {
    co: BTreeMap<usize, i128>,
    c: i128,
}

/// Whether some values make every formula true. `atoms` holds the
/// constraints already taken from the formulas that are settled, and each
/// `or` met is decided by trying its sides in turn.
fn satisfiable(mut atoms: Vec<Con>, pending: Vec<&Formula>) -> Res<bool> {
    let mut choices: Vec<&[Formula]> = Vec::new();
    let mut stack = pending;
    while let Some(f) = stack.pop() {
        match f {
            Formula::Ge(l) => atoms.push(Con {
                co: l.terms.clone(),
                c: l.constant,
            }),
            Formula::And(items) => stack.extend(items),
            Formula::Or(items) => choices.push(items),
        }
    }
    if !feasible(atoms.clone())? {
        return Ok(false);
    }
    let Some((first, rest)) = choices.split_first() else {
        return Ok(true);
    };
    for side in first.iter() {
        // The choices left are decided in turn, each as a formula of its own.
        let mut more: Vec<&Formula> = vec![side];
        let lefts: Vec<Formula> = rest.iter().map(|c| Formula::Or(c.to_vec())).collect();
        more.extend(lefts.iter());
        if satisfiable(atoms.clone(), more)? {
            return Ok(true);
        }
    }
    Ok(false)
}

/// What normalizing a constraint finds.
enum Norm {
    /// It holds whatever the values are.
    Always,
    /// It holds for no values.
    Never,
    Keep(Con),
}

/// Divides a constraint by what its numbers have in common, which for
/// whole numbers rounds the constant down, so that `2x + 1 >= 0` is
/// `x >= 0`.
fn normalize(mut con: Con) -> Norm {
    con.co.retain(|_, k| *k != 0);
    let g = con.co.values().fold(0, |g, k| gcd(g, *k));
    if g == 0 {
        return if con.c >= 0 {
            Norm::Always
        } else {
            Norm::Never
        };
    }
    if g > 1 {
        for k in con.co.values_mut() {
            *k /= g;
        }
        con.c = con.c.div_euclid(g);
    }
    Norm::Keep(con)
}

/// Whether some whole numbers make every constraint hold, by the Omega test.
fn feasible(mut cs: Vec<Con>) -> Res<bool> {
    loop {
        let mut kept: BTreeMap<Vec<(usize, i128)>, Con> = BTreeMap::new();
        for con in cs {
            match normalize(con) {
                Norm::Never => return Ok(false),
                Norm::Always => {}
                Norm::Keep(con) => {
                    let key: Vec<(usize, i128)> = con.co.iter().map(|(v, k)| (*v, *k)).collect();
                    match kept.get_mut(&key) {
                        Some(old) if old.c <= con.c => {}
                        _ => {
                            kept.insert(key, con);
                        }
                    }
                }
            }
        }
        // Two constraints that bound a sum from both sides at once are an
        // equation, which is solved for a variable.
        let mut equation = None;
        for (key, con) in &kept {
            let opposite: Vec<(usize, i128)> = key.iter().map(|(v, k)| (*v, -k)).collect();
            if let Some(other) = kept.get(&opposite) {
                match add(con.c, other.c)? {
                    s if s < 0 => return Ok(false),
                    0 => {
                        // The equation whose smallest number is the smallest
                        // is solved first, so that one with a number 1 is.
                        let smallest = |c: &Con| c.co.values().map(|k| k.abs()).min();
                        if equation
                            .as_ref()
                            .is_none_or(|e| smallest(con) < smallest(e))
                        {
                            equation = Some(con.clone());
                        }
                    }
                    _ => {}
                }
            }
        }
        cs = kept.into_values().collect();
        if let Some(eq) = equation {
            cs = solve_equation(cs, &eq)?;
            continue;
        }
        let mut vars: Vec<usize> = cs.iter().flat_map(|c| c.co.keys().copied()).collect();
        vars.sort_unstable();
        vars.dedup();
        if vars.is_empty() {
            return Ok(true);
        }
        // A variable with bounds on one side only can always be satisfied,
        // so what mentions it is left out.
        let one_sided = vars.iter().copied().find(|v| {
            let (mut below, mut above) = (false, false);
            for con in &cs {
                match con.co.get(v) {
                    Some(k) if *k > 0 => below = true,
                    Some(_) => above = true,
                    None => {}
                }
            }
            !(below && above)
        });
        if let Some(v) = one_sided {
            cs.retain(|c| !c.co.contains_key(&v));
            continue;
        }
        // The variable whose elimination is exact, or else the one with the
        // fewest pairs of bounds.
        let mut best: Option<(bool, usize, usize)> = None;
        for v in &vars {
            let (mut lowers, mut uppers) = (0usize, 0usize);
            let (mut lower_unit, mut upper_unit) = (true, true);
            for con in &cs {
                match con.co.get(v) {
                    Some(k) if *k > 0 => {
                        lowers += 1;
                        lower_unit &= *k == 1;
                    }
                    Some(k) => {
                        uppers += 1;
                        upper_unit &= *k == -1;
                    }
                    None => {}
                }
            }
            let candidate = (!(lower_unit || upper_unit), lowers * uppers, *v);
            if best.is_none_or(|b| candidate < b) {
                best = Some(candidate);
            }
        }
        let (inexact, _, v) = best.expect("a variable");
        let (lowers, uppers, others) = split(&cs, v);
        let real = pairs(&lowers, &uppers, v, false)?;
        if !inexact {
            cs = [others, real].concat();
            continue;
        }
        if !feasible([others.clone(), real].concat())? {
            return Ok(false);
        }
        let dark = pairs(&lowers, &uppers, v, true)?;
        if feasible([others, dark].concat())? {
            return Ok(true);
        }
        // Whole numbers that fit between the bounds of the real shadow but
        // not of the dark one are near a lower bound.
        let widest = uppers.iter().map(|u| -u.co[&v]).max().expect("an upper");
        for lower in &lowers {
            let a = lower.co[&v];
            let most = sub(sub(mul(a, widest)?, a)?, widest)? / widest;
            let mut i = 0;
            while i <= most {
                let mut up = lower.clone();
                up.c = sub(up.c, i)?;
                let mut down = Con {
                    co: BTreeMap::new(),
                    c: add(-up.c, 0)?,
                };
                for (w, k) in &up.co {
                    down.co.insert(*w, -k);
                }
                let mut all = cs.clone();
                all.push(up);
                all.push(down);
                if feasible(all)? {
                    return Ok(true);
                }
                i += 1;
            }
        }
        return Ok(false);
    }
}

/// The constraints that bound `v` from below, those that bound it from
/// above, and those that do not mention it.
fn split(cs: &[Con], v: usize) -> (Vec<Con>, Vec<Con>, Vec<Con>) {
    let (mut lowers, mut uppers, mut others) = (Vec::new(), Vec::new(), Vec::new());
    for con in cs {
        match con.co.get(&v) {
            Some(k) if *k > 0 => lowers.push(con.clone()),
            Some(_) => uppers.push(con.clone()),
            None => others.push(con.clone()),
        }
    }
    (lowers, uppers, others)
}

/// What each lower bound and each upper bound of `v` say of the rest, once
/// `v` is left out. With `dark`, the constraints are those that make a whole
/// number between them certain.
fn pairs(lowers: &[Con], uppers: &[Con], v: usize, dark: bool) -> Res<Vec<Con>> {
    let mut out = Vec::new();
    for lower in lowers {
        for upper in uppers {
            let (a, b) = (lower.co[&v], -upper.co[&v]);
            let mut con = Con {
                co: BTreeMap::new(),
                c: add(mul(b, lower.c)?, mul(a, upper.c)?)?,
            };
            for (w, k) in &lower.co {
                if *w != v {
                    let entry = con.co.entry(*w).or_insert(0);
                    *entry = add(*entry, mul(b, *k)?)?;
                }
            }
            for (w, k) in &upper.co {
                if *w != v {
                    let entry = con.co.entry(*w).or_insert(0);
                    *entry = add(*entry, mul(a, *k)?)?;
                }
            }
            if dark {
                con.c = sub(con.c, mul(a - 1, b - 1)?)?;
            }
            out.push(con);
        }
    }
    Ok(out)
}

/// The constraints with a variable replaced by a sum.
fn replace(cs: Vec<Con>, v: usize, by: &Con) -> Res<Vec<Con>> {
    let mut out = Vec::new();
    for mut con in cs {
        if let Some(k) = con.co.remove(&v) {
            for (w, n) in &by.co {
                let entry = con.co.entry(*w).or_insert(0);
                *entry = add(*entry, mul(k, *n)?)?;
            }
            con.c = add(con.c, mul(k, by.c)?)?;
        }
        out.push(con);
    }
    Ok(out)
}

/// The constraints with the equation `eq = 0` used to leave a variable out.
fn solve_equation(cs: Vec<Con>, eq: &Con) -> Res<Vec<Con>> {
    // A variable whose number is 1 or -1 is the sum of the others.
    if let Some((v, a)) = eq.co.iter().find(|(_, k)| k.abs() == 1) {
        let (v, a) = (*v, *a);
        let mut by = Con {
            co: BTreeMap::new(),
            c: mul(-a, eq.c)?,
        };
        for (w, k) in &eq.co {
            if *w != v {
                by.co.insert(*w, mul(-a, *k)?);
            }
        }
        return replace(cs, v, &by);
    }
    // Otherwise a new variable is chosen so that the numbers of the equation
    // get smaller, as Pugh describes.
    let (&k, &a) = eq
        .co
        .iter()
        .min_by_key(|(v, a)| (a.abs(), **v))
        .expect("a variable");
    let m = a.abs() + 1;
    let sigma = cs
        .iter()
        .chain([eq])
        .flat_map(|c| c.co.keys().copied())
        .max()
        .map_or(0, |v| v + 1);
    let s = a.signum();
    let hat = |n: i128| -> Res<i128> {
        let twice = add(mul(2, n)?, m)?;
        sub(n, mul(m, twice.div_euclid(mul(2, m)?))?)
    };
    let mut by = Con {
        co: BTreeMap::new(),
        c: mul(s, hat(eq.c)?)?,
    };
    by.co.insert(sigma, mul(-s, m)?);
    for (w, n) in &eq.co {
        if *w != k {
            by.co.insert(*w, mul(s, hat(*n)?)?);
        }
    }
    replace(cs, k, &by)
}
