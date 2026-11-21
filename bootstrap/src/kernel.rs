//! The kernel: it works a core term out, one step at a time, and trusts no
//! compiled code. It depends on the core term and on nothing else of the
//! checker. A step is one node worked out, and a run ends in a value, in a
//! failure to end within the steps it was given, or in a term that is not a
//! value it can work out.

use crate::core::{Callee, Function, Kind, Lit, Make, Operator, Pattern, Source, Term};
use crate::diag::Pos;

/// What a term is worked out to.
#[derive(Clone, Debug, PartialEq)]
pub enum Value {
    Int(i128),
    Text(Vec<u8>),
    /// A struct, a tuple, or a list, with its parts in order.
    Parts(Vec<Value>),
    /// A case of an enum, by its place, with what it holds.
    Case(usize, Vec<Value>),
    /// A function written where it is used, with the names it was made among.
    Closure(Vec<String>, Term, Vec<(String, Value)>),
    /// The end of a round of a loop: whether it goes on, and the state.
    Round(bool, Vec<Value>),
}

/// Why a `bool` was what it was: the comparison that decided it, where it is
/// written, and the numbers it compared.
#[derive(Clone, Debug, PartialEq)]
pub struct Why {
    pub pos: Pos,
    /// The comparison with the numbers it compared, as `9 < 3`.
    pub comparison: String,
    /// Whether it held.
    pub holds: bool,
}

impl Why {
    /// What it says: `9 < 3` is false.
    pub fn text(&self) -> String {
        format!("`{}` is {}", self.comparison, self.holds)
    }
}

/// What stopped a run.
#[derive(Clone, Debug, PartialEq)]
pub enum Failure {
    /// The run took more steps than it was given.
    Limit(u64),
    /// A term the kernel does not work out, with what it is.
    Stuck(String),
}

/// A run over the functions of a file.
pub struct Machine<'a> {
    functions: &'a [Function],
    steps: u64,
    limit: u64,
    last: Option<Why>,
}

type Env = Vec<(String, Value)>;

/// A value, and for a `bool` why it is what it is.
type Worked = (Value, Option<Why>);

impl<'a> Machine<'a> {
    pub fn new(functions: &'a [Function], limit: u64) -> Machine<'a> {
        Machine {
            functions,
            steps: 0,
            limit,
            last: None,
        }
    }

    /// The last comparison worked out, which is what a `bool` that no
    /// comparison gave directly, as a `var` a loop handed on, is last owed to.
    pub fn last_comparison(&self) -> Option<&Why> {
        self.last.as_ref()
    }

    /// How many steps have been taken.
    pub fn steps(&self) -> u64 {
        self.steps
    }

    /// The function of the file at `place`, called with `args`.
    pub fn call(&mut self, place: usize, args: Vec<Value>) -> Result<Worked, Failure> {
        let f = self
            .functions
            .iter()
            .find(|f| f.place == place)
            .ok_or_else(|| Failure::Stuck(format!("function {place} has no core term")))?;
        let mut env: Env = f.params.iter().map(|(n, _)| n.clone()).zip(args).collect();
        self.work(&f.body, &mut env)
    }

    fn work(&mut self, t: &Term, env: &mut Env) -> Result<Worked, Failure> {
        self.steps += 1;
        if self.steps > self.limit {
            return Err(Failure::Limit(self.limit));
        }
        let plain = |v: Value| Ok((v, None));
        match &t.kind {
            Kind::Lit(Lit::Int(n)) => plain(Value::Int(*n)),
            Kind::Lit(Lit::Text(text)) => plain(Value::Text(text.clone())),
            Kind::Name(n) => match env.iter().rfind(|(k, _)| k == n) {
                Some((_, v)) => plain(v.clone()),
                None => Err(Failure::Stuck(format!("`{n}` is not bound"))),
            },
            Kind::Make(make, args) => {
                let parts = self.all(args, env)?;
                plain(match make {
                    Make::Case(_, case) => Value::Case(*case, parts),
                    Make::Struct(_) | Make::Tuple | Make::List => Value::Parts(parts),
                })
            }
            Kind::Field { of, index, .. } => match self.work(of, env)?.0 {
                Value::Parts(parts) => plain(parts[*index].clone()),
                other => Err(Failure::Stuck(format!("{other:?} has no fields"))),
            },
            Kind::Call {
                callee: Callee::Operator(op @ (Operator::Size | Operator::Bytes)),
                args,
            } => {
                let of = self.work(&args[0], env)?.0;
                match (op, of) {
                    (Operator::Size, Value::Text(t)) => plain(Value::Int(t.len() as i128)),
                    (Operator::Size, Value::Parts(p)) => plain(Value::Int(p.len() as i128)),
                    (Operator::Bytes, Value::Text(t)) => plain(Value::Parts(
                        t.iter().map(|b| Value::Int(*b as i128)).collect(),
                    )),
                    (op, other) => Err(Failure::Stuck(format!("`{}` of {other:?}", op.symbol()))),
                }
            }
            Kind::Call {
                callee: Callee::Operator(Operator::Index),
                args,
            } => {
                let (of, at) = (self.work(&args[0], env)?.0, self.work(&args[1], env)?.0);
                match (of, at) {
                    (Value::Parts(items), Value::Int(i)) => {
                        match usize::try_from(i).ok().and_then(|i| items.get(i)) {
                            Some(item) => plain(item.clone()),
                            None => Err(Failure::Stuck(format!(
                                "an index {i} of a list of {}",
                                items.len()
                            ))),
                        }
                    }
                    other => Err(Failure::Stuck(format!("an index of {other:?}"))),
                }
            }
            Kind::Call {
                callee: Callee::Operator(op),
                args,
            } => {
                let (a, b) = (self.work(&args[0], env)?.0, self.work(&args[1], env)?.0);
                let (Value::Int(x), Value::Int(y)) = (&a, &b) else {
                    return Err(Failure::Stuck(format!(
                        "`{}` of {a:?} and {b:?}",
                        op.symbol()
                    )));
                };
                let (x, y) = (*x, *y);
                let mut truth = |b: bool| {
                    let why = Why {
                        pos: t.pos,
                        comparison: format!("{x} {} {y}", op.symbol()),
                        holds: b,
                    };
                    self.last = Some(why.clone());
                    Ok((Value::Case(usize::from(b), Vec::new()), Some(why)))
                };
                match op {
                    Operator::Add => plain(Value::Int(x + y)),
                    Operator::Sub => plain(Value::Int(x - y)),
                    Operator::Mul => plain(Value::Int(x * y)),
                    Operator::Less => truth(x < y),
                    Operator::LessEqual => truth(x <= y),
                    Operator::Greater => truth(x > y),
                    Operator::GreaterEqual => truth(x >= y),
                    Operator::Equal => truth(x == y),
                    Operator::Index | Operator::Size | Operator::Bytes => {
                        unreachable!("worked out above")
                    }
                }
            }
            Kind::Call {
                callee: Callee::Function(place),
                args,
            } => {
                let args = self.all(args, env)?;
                self.call(*place, args)
            }
            Kind::Call { callee, .. } => Err(Failure::Stuck(format!("a call of {callee:?}"))),
            Kind::Bind { name, value, rest } => {
                let v = self.work(value, env)?.0;
                env.push((name.clone(), v));
                let r = self.work(rest, env);
                env.pop();
                r
            }
            Kind::Lambda { params, body } => plain(Value::Closure(
                params.iter().map(|(n, _)| n.clone()).collect(),
                (**body).clone(),
                env.clone(),
            )),
            Kind::Apply { function, args } => {
                let Value::Closure(names, body, mut held) = self.work(function, env)?.0 else {
                    return Err(Failure::Stuck("a call of what is not a function".into()));
                };
                for (n, a) in names.iter().zip(args) {
                    let v = self.work(a, env)?.0;
                    held.push((n.clone(), v));
                }
                self.work(&body, &mut held)
            }
            Kind::Next(values) => plain(Value::Round(true, self.all(values, env)?)),
            Kind::Done(values) => plain(Value::Round(false, self.all(values, env)?)),
            Kind::Loop {
                source,
                counter,
                state,
                body,
            } => self.round_loop(source, counter, state, body, env),
            Kind::Choose { of, arms } => {
                let (v, why) = self.work(of, env)?;
                for (p, body) in arms {
                    let mark = env.len();
                    if matches(p, &v, env) {
                        let (r, own) = self.work(body, env)?;
                        env.truncate(mark);
                        // A block that is a `bool` written out is decided by
                        // what was tested, as `and`, `or`, and `not` are.
                        let inherits =
                            matches!(&body.kind, Kind::Make(Make::Case(..), a) if a.is_empty());
                        return Ok((r, if inherits { why } else { own }));
                    }
                    env.truncate(mark);
                }
                Err(Failure::Stuck(format!("no arm for {v:?}")))
            }
        }
    }

    /// A loop, one round at a time.
    fn round_loop(
        &mut self,
        source: &Source,
        counter: &str,
        state: &[(String, Term)],
        body: &Term,
        env: &mut Env,
    ) -> Result<Worked, Failure> {
        let mut held: Vec<Value> = state
            .iter()
            .map(|(_, t)| Ok(self.work(t, env)?.0))
            .collect::<Result<_, Failure>>()?;
        let (mut at, end, items): (i128, i128, Option<Vec<Value>>) = match source {
            Source::Range(a, b) => match (self.work(a, env)?.0, self.work(b, env)?.0) {
                (Value::Int(a), Value::Int(b)) => (a, b, None),
                other => return Err(Failure::Stuck(format!("a range of {other:?}"))),
            },
            Source::Over(of) => match self.work(of, env)?.0 {
                Value::Parts(items) => (0, items.len() as i128, Some(items)),
                Value::Text(text) => {
                    let bytes: Vec<Value> = text.iter().map(|b| Value::Int(*b as i128)).collect();
                    (0, bytes.len() as i128, Some(bytes))
                }
                other => return Err(Failure::Stuck(format!("a loop over {other:?}"))),
            },
            Source::Open => (0, i128::MAX, None),
        };
        loop {
            if at >= end {
                break;
            }
            let item = match &items {
                Some(items) => items[at as usize].clone(),
                None => Value::Int(at),
            };
            at += 1;
            let mark = env.len();
            env.push((counter.to_string(), item));
            for ((n, _), v) in state.iter().zip(&held) {
                env.push((n.clone(), v.clone()));
            }
            let round = self.work(body, env);
            env.truncate(mark);
            match round?.0 {
                Value::Round(more, values) => {
                    held = values;
                    if !more {
                        break;
                    }
                }
                other => return Err(Failure::Stuck(format!("a round that gave {other:?}"))),
            }
        }
        Ok((Value::Parts(held), None))
    }

    fn all(&mut self, terms: &[Term], env: &mut Env) -> Result<Vec<Value>, Failure> {
        terms.iter().map(|t| Ok(self.work(t, env)?.0)).collect()
    }
}

fn matches(p: &Pattern, v: &Value, env: &mut Env) -> bool {
    match (p, v) {
        (Pattern::Any, _) => true,
        (Pattern::Bind(n), v) => {
            env.push((n.clone(), v.clone()));
            true
        }
        (Pattern::Lit(Lit::Int(n)), Value::Int(m)) => n == m,
        (Pattern::Lit(Lit::Text(t)), Value::Text(u)) => t == u,
        (Pattern::Case(c, inner), Value::Case(d, held)) => {
            c == d
                && match inner {
                    None => true,
                    Some(p) => held.first().is_some_and(|h| matches(p, h, env)),
                }
        }
        (Pattern::Tuple(ps), Value::Parts(vs)) => {
            ps.len() == vs.len() && ps.iter().zip(vs).all(|(p, v)| matches(p, v, env))
        }
        _ => false,
    }
}
