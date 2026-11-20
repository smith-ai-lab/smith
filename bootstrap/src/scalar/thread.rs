//! The pass after folding: a jump into a block that only chooses goes to the
//! block the choice would take.
//!
//! One walk over each function's blocks, from the last to the first, with no
//! walk to find what to change. A block that a jump goes into has been sent on
//! by then when it comes later in the function, so the jump goes where it goes
//! in one step, and a chain of blocks is not gone through again from each block
//! of it.
//! A block counts as one to go through when it writes no memory and calls
//! nothing, and no value it defines is read by another block, so skipping it
//! leaves nothing unmade. A jump into such a block gives its parameters
//! values, and the pass works out what the block does with them as folding
//! would: a value is known when it is a constant, or is worked out from known
//! values. When the block ends with a jump, or with a choice on a known
//! value, the jump that led into it goes where the block goes, giving what the
//! block gave, and the walk goes on from there, so a chain of such blocks is
//! gone through in one step each, and at most as many as the function has
//! blocks.
//!
//! A block that is jumped to from a block that ends with one jump, giving it
//! a comparison's value, or a choice between constants on a comparison's
//! value, makes the choice on the comparison itself, so the end of the block
//! that jumped is a choice with the two answers. The block that was gone
//! through stays for the jumps that could not be sent past it, and `remove`
//! takes it away when none is left.

use super::eval::{binary, bits_of, compare, mask, narrow, widen};
use super::{Constant, End, Function, Instruction, Jump, Op, Place, Program, Type, Value};

/// The program with each jump that goes into a block that only chooses sent
/// on to the block the choice takes.
pub fn thread(program: Program) -> Program {
    Program {
        data: program.data,
        functions: program.functions.into_iter().map(threaded).collect(),
        main: program.main,
    }
}

/// What is known of a value that a block is given or makes.
#[derive(Clone, Copy)]
struct Bind {
    /// The number it stands for, when it is known.
    known: Option<u64>,
    /// The value it is, in the block that jumps, when there is one.
    value: Option<Value>,
}

/// What a jump gives a block, in the block that jumps.
#[derive(Clone, Copy)]
enum Arg {
    Value(Value),
    Const(u64, Type),
}

/// A choice between two jumps, or one jump.
enum Step {
    One(Pending),
    Split { on: Value, jumps: Vec<Pending> },
}

/// What a function says about its values, which the walk reads.
struct Facts {
    /// The number each value is, when an instruction makes it a constant.
    constant: Vec<Option<u64>>,
    /// Whether an instruction compares to make the value, so that it is 0 or 1.
    compared: Vec<bool>,
    /// The choice that makes a value, as the value chosen on and the two
    /// values chosen between.
    chosen: Vec<Option<(Value, Value, Value)>>,
    /// Whether a block can be gone through.
    through: Vec<bool>,
    /// What each `resolve` uses for what it knows of the values of a block,
    /// kept so that each does not ask for one.
    scratch: std::cell::RefCell<Env>,
}

fn threaded(mut f: Function) -> Function {
    let n = f.blocks.len();
    let reached = reached_by(&f);
    localize(&mut f, &reached);
    let mut facts = facts(&f, &reached);

    for a in (0..n).rev() {
        let mut extra: Vec<Instruction> = Vec::new();
        let end = f.blocks[a].end.clone();
        let new = match end {
            End::Jump(j) => follow(&mut f, &mut facts, &mut extra, a, j, true),
            End::Switch { on, jumps } => {
                let jumps: Vec<Jump> = jumps
                    .into_iter()
                    .map(
                        |j| match follow(&mut f, &mut facts, &mut extra, a, j, false) {
                            End::Jump(j) => j,
                            _ => unreachable!("a jump that may not split gives one jump"),
                        },
                    )
                    .collect();
                End::Switch { on, jumps }
            }
            End::Return(values) => End::Return(values),
        };
        if extra.is_empty() && new == f.blocks[a].end {
            continue;
        }
        f.blocks[a].instructions.extend(extra);
        f.blocks[a].end = new;
    }
    f
}

/// What a block defines and only the block it alone jumps to reads is given to
/// that block as a parameter, so that going through the first leaves the
/// second what it reads. The block jumped to is read as the parameter, and
/// each jump into it gives the value.
fn localize(f: &mut Function, reached: &[bool]) {
    let values = f.types.len();
    let defined = defined_in(f);
    // A constant that a block the pass may go through defines, and another
    // block reads, is made again in the block that reads it, so that it does
    // not keep the first from being gone through.
    let mut constant: Vec<Option<Constant>> = vec![None; values];
    let mut effects = vec![false; f.blocks.len()];
    for (b, block) in f.blocks.iter().enumerate() {
        for i in &block.instructions {
            match (&i.op, &i.results[..]) {
                (Op::Constant(c), [r]) => constant[r.0] = Some(*c),
                (Op::Store { .. } | Op::Call { .. } | Op::System { .. }, _) => effects[b] = true,
                _ => {}
            }
        }
    }
    let wanted = |v: Value, b: usize| {
        let d = defined[v.0];
        constant[v.0].is_some() && d != b && d != usize::MAX && !effects[d] && d != 0
    };
    // The value made again for each value that a block reads, while the block
    // is made, which is empty between blocks.
    let mut again = vec![usize::MAX; values];
    for b in 0..f.blocks.len() {
        let mut any = false;
        for i in &f.blocks[b].instructions {
            each_read(&i.op, |v| any |= wanted(v, b));
        }
        each_end_read(&f.blocks[b].end, |v| any |= wanted(v, b));
        if !any {
            continue;
        }
        let mut made: Vec<Value> = Vec::new();
        let mut instructions: Vec<Instruction> = Vec::new();
        let mut by = |v: Value, types: &mut Vec<Type>| {
            let d = defined[v.0];
            let Some(c) = constant[v.0] else { return v };
            if d == b || d == usize::MAX || effects[d] || d == 0 {
                return v;
            }
            if again[v.0] != usize::MAX {
                return Value(again[v.0]);
            }
            let q = Value(types.len());
            types.push(types[v.0]);
            made.push(v);
            again[v.0] = q.0;
            instructions.push(Instruction {
                results: vec![q],
                op: Op::Constant(c),
                at: Place::NONE,
            });
            q
        };
        let mut block = std::mem::replace(&mut f.blocks[b], empty_block());
        for i in &mut block.instructions {
            let mut ops = Vec::new();
            each_read(&i.op, |v| ops.push((v, by(v, &mut f.types))));
            let lookup = |v: Value| ops.iter().find(|(from, _)| *from == v).map_or(v, |p| p.1);
            i.op.rename(&lookup);
        }
        let mut ops = Vec::new();
        each_end_read(&block.end, |v| ops.push((v, by(v, &mut f.types))));
        let lookup = |v: Value| ops.iter().find(|(from, _)| *from == v).map_or(v, |p| p.1);
        super::graph::rename_end(&mut block.end, &lookup);
        for v in made {
            again[v.0] = usize::MAX;
        }
        instructions.append(&mut block.instructions);
        block.instructions = instructions;
        f.blocks[b] = block;
    }
    let values = f.types.len();
    let defined = defined_in(f);
    // The blocks that read each value outside the block that defines it.
    let mut readers: Vec<Vec<usize>> = vec![Vec::new(); values];
    let mut into = vec![0usize; f.blocks.len()];
    // The block that jumps to a block that is jumped to once.
    let mut from = vec![usize::MAX; f.blocks.len()];
    for (b, block) in f.blocks.iter().enumerate() {
        for j in super::graph::jumps(&block.end) {
            into[j.block] += 1;
            from[j.block] = b;
        }
    }
    for (b, block) in f.blocks.iter().enumerate().filter(|(b, _)| reached[*b]) {
        // The blocks are taken one after the other, so a block that reads a
        // value twice is the last one noted for it.
        let mut note = |v: Value| {
            if defined[v.0] != b && defined[v.0] != usize::MAX && readers[v.0].last() != Some(&b) {
                readers[v.0].push(b);
            }
        };
        for i in &block.instructions {
            each_read(&i.op, &mut note);
        }
        each_end_read(&block.end, &mut note);
    }
    let mut given: Vec<Vec<(Value, Value)>> = vec![Vec::new(); f.blocks.len()];
    // For the value being given on: the parameter each block on its path is
    // given, by block, and which values these belong to, so that neither is
    // searched for.
    let mut param_of = vec![usize::MAX; f.blocks.len()];
    for v in 0..values {
        if readers[v].is_empty() {
            continue;
        }
        let b = defined[v];
        // The blocks from the one that defines it to each that reads it,
        // each jumped to once, by the one before.
        let mut path: Vec<usize> = Vec::new();
        let mut alone = true;
        for &t in &readers[v] {
            let mut at = t;
            let mut steps = 0;
            while at != b {
                if at == 0 || into[at] != 1 || steps > f.blocks.len() {
                    alone = false;
                    break;
                }
                if param_of[at] == usize::MAX {
                    param_of[at] = 0;
                    path.push(at);
                }
                at = from[at];
                steps += 1;
            }
        }
        if !alone {
            for &c in &path {
                param_of[c] = usize::MAX;
            }
            continue;
        }
        for &c in &path {
            let q = Value(f.types.len());
            f.types.push(f.types[v]);
            f.blocks[c].params.push(q);
            param_of[c] = q.0;
        }
        for &c in &path {
            let before = from[c];
            let arg = if param_of[before] != usize::MAX {
                Value(param_of[before])
            } else {
                Value(v)
            };
            for j in super::graph::jumps_mut(&mut f.blocks[before].end) {
                if j.block == c {
                    j.args.push(arg);
                }
            }
        }
        for &t in &readers[v] {
            given[t].push((Value(v), Value(param_of[t])));
        }
        for &c in &path {
            param_of[c] = usize::MAX;
        }
    }
    let mut renamed = vec![usize::MAX; f.types.len()];
    for (t, pairs) in given.iter().enumerate() {
        if pairs.is_empty() {
            continue;
        }
        for &(from, to) in pairs {
            renamed[from.0] = to.0;
        }
        let by = |v: Value| match renamed[v.0] {
            usize::MAX => v,
            q => Value(q),
        };
        let block = &mut f.blocks[t];
        for i in &mut block.instructions {
            i.op.rename(&by);
        }
        super::graph::rename_end(&mut block.end, &by);
        for &(from, _) in pairs {
            renamed[from.0] = usize::MAX;
        }
    }
}

/// The block that defines each value, which is `usize::MAX` for one that no
/// block defines.
fn defined_in(f: &Function) -> Vec<usize> {
    let mut defined = vec![usize::MAX; f.types.len()];
    for (b, block) in f.blocks.iter().enumerate() {
        for &p in &block.params {
            defined[p.0] = b;
        }
        for i in &block.instructions {
            for r in &i.results {
                defined[r.0] = b;
            }
        }
    }
    defined
}

/// Whether each block is reached from the first.
fn reached_by(f: &Function) -> Vec<bool> {
    let mut reached = vec![false; f.blocks.len()];
    for b in super::graph::reverse_postorder(f) {
        reached[b] = true;
    }
    reached
}

/// What is known of the values a block is given and makes, by the number of the
/// value, so that asking for one takes no search. The places that were set are
/// kept, so that making it empty again takes as long as they are many and not
/// as long as the function has values.
#[derive(Default)]
struct Env {
    binds: Vec<Option<Bind>>,
    set: Vec<usize>,
}

impl Env {
    fn get(&self, v: Value) -> Option<&Bind> {
        self.binds.get(v.0).and_then(Option::as_ref)
    }

    fn insert(&mut self, v: Value, bind: Bind) {
        if self.binds.len() <= v.0 {
            self.binds.resize(v.0 + 1, None);
        }
        self.binds[v.0] = Some(bind);
        self.set.push(v.0);
    }

    fn clear(&mut self) {
        for v in self.set.drain(..) {
            self.binds[v] = None;
        }
    }
}

/// Calls `f` with each value the op reads, in order.
fn each_read(op: &Op, mut f: impl FnMut(Value)) {
    match op {
        Op::Constant(_) => {}
        Op::Binary { a, b, .. }
        | Op::Compare { a, b, .. }
        | Op::Overflows { a, b, .. }
        | Op::FloatBinary { a, b, .. }
        | Op::FloatCompare { a, b, .. }
        | Op::VectorBinary { a, b, .. }
        | Op::VectorFloatBinary { a, b, .. }
        | Op::VectorCompare { a, b, .. }
        | Op::VectorFloatCompare { a, b, .. } => {
            f(*a);
            f(*b);
        }
        Op::FloatFused { a, b, c, .. } | Op::VectorFloatFused { a, b, c, .. } => {
            f(*a);
            f(*b);
            f(*c);
        }
        Op::Widen { value, .. }
        | Op::Narrow(value)
        | Op::FloatNegate(value)
        | Op::FloatFromInt { value, .. }
        | Op::IntFromFloat { value, .. }
        | Op::FloatResize(value)
        | Op::VectorSplat(value)
        | Op::VectorSum(value)
        | Op::VectorFirstSet(value)
        | Op::VectorExtract { vector: value, .. } => f(*value),
        Op::Choose {
            on,
            then,
            otherwise,
        }
        | Op::VectorChoose {
            mask: on,
            then,
            otherwise,
        } => {
            f(*on);
            f(*then);
            f(*otherwise);
        }
        Op::VectorInsert { vector, value, .. } => {
            f(*vector);
            f(*value);
        }
        Op::Load { address, .. } => f(*address),
        Op::Store { address, value, .. } => {
            f(*address);
            f(*value);
        }
        Op::Call { args, .. } | Op::System { args, .. } => args.iter().for_each(|&v| f(v)),
    }
}

/// Calls `f` with each value the end reads, in order.
fn each_end_read(end: &End, mut f: impl FnMut(Value)) {
    match end {
        End::Jump(j) => j.args.iter().for_each(|&v| f(v)),
        End::Switch { on, jumps } => {
            f(*on);
            jumps.iter().flat_map(|j| &j.args).for_each(|&v| f(v));
        }
        End::Return(values) => values.iter().for_each(|&v| f(v)),
    }
}

fn empty_block() -> super::Block {
    super::Block {
        params: Vec::new(),
        instructions: Vec::new(),
        end: End::Return(Vec::new()),
    }
}

fn facts(f: &Function, reached: &[bool]) -> Facts {
    let values = f.types.len();
    let mut constant = vec![None; values];
    let mut compared = vec![false; values];
    let mut chosen = vec![None; values];
    let mut defined = vec![usize::MAX; values];
    let mut through = vec![true; f.blocks.len()];
    through[0] = false;
    for (b, block) in f.blocks.iter().enumerate() {
        for &p in &block.params {
            defined[p.0] = b;
        }
        for i in &block.instructions {
            for r in &i.results {
                defined[r.0] = b;
            }
            match (&i.op, &i.results[..]) {
                (Op::Constant(Constant::Int(c)), [r]) => {
                    constant[r.0] = Some(c & mask(bits_of(f.types[r.0])));
                }
                (Op::Compare { .. }, [r]) => compared[r.0] = true,
                (
                    Op::Choose {
                        on,
                        then,
                        otherwise,
                    },
                    [r],
                ) => chosen[r.0] = Some((*on, *then, *otherwise)),
                (
                    Op::Store { .. } | Op::Call { .. } | Op::System { .. } | Op::Overflows { .. },
                    _,
                ) => {
                    through[b] = false;
                }
                _ => {}
            }
        }
    }
    // A block whose value is read in another block is not gone through. A
    // block that nothing reaches reads nothing.
    for (b, block) in f.blocks.iter().enumerate().filter(|(b, _)| reached[*b]) {
        let mut note = |v: Value| {
            let d = defined[v.0];
            if d != b && d != usize::MAX {
                through[d] = false;
            }
        };
        for i in &block.instructions {
            each_read(&i.op, &mut note);
        }
        each_end_read(&block.end, &mut note);
    }
    Facts {
        constant,
        compared,
        chosen,
        through,
        scratch: Default::default(),
    }
}

/// A jump that is not made yet: the block it goes to and what it gives.
type Pending = (usize, Vec<Arg>);

/// The end that a jump from block `a` is sent on to, going through as many
/// blocks as it can. The jump is made once, where the walk ends, so that a
/// constant is made for the numbers that jump gives and for no jump the walk
/// went past.
fn follow(
    f: &mut Function,
    facts: &mut Facts,
    extra: &mut Vec<Instruction>,
    a: usize,
    jump: Jump,
    may_split: bool,
) -> End {
    let mut pending: Pending = (
        jump.block,
        jump.args.iter().map(|&v| Arg::Value(v)).collect(),
    );
    let mut moved = false;
    for _ in 0..f.blocks.len() {
        match step(f, facts, a, &pending, may_split) {
            None => break,
            Some(Step::One(next)) => {
                pending = next;
                moved = true;
            }
            Some(Step::Split { on, jumps }) => {
                // Both jumps are made before either goes on, so that the
                // values the constants are given do not depend on how far each
                // goes.
                let jumps: Vec<Jump> = jumps
                    .into_iter()
                    .map(|p| make(f, facts, extra, p))
                    .collect();
                let jumps = jumps
                    .into_iter()
                    .map(|j| match follow(f, facts, extra, a, j, false) {
                        End::Jump(j) => j,
                        _ => unreachable!("a jump that may not split gives one jump"),
                    })
                    .collect();
                return End::Switch { on, jumps };
            }
        }
    }
    if moved {
        End::Jump(make(f, facts, extra, pending))
    } else {
        End::Jump(jump)
    }
}

/// What a block is given by a jump, as the block that is jumped to reads it.
fn bind_of(facts: &Facts, arg: Arg) -> Bind {
    match arg {
        Arg::Value(v) => Bind {
            known: facts.constant[v.0],
            value: Some(v),
        },
        Arg::Const(c, _) => Bind {
            known: Some(c),
            value: None,
        },
    }
}

/// Where the jump goes once the block it jumps to is gone through, if it can
/// be.
fn step(f: &Function, facts: &Facts, a: usize, jump: &Pending, may_split: bool) -> Option<Step> {
    let (b, args) = (jump.0, &jump.1);
    if b == a || !facts.through[b] {
        return None;
    }
    let block = &f.blocks[b];
    let binds: Vec<Bind> = args.iter().map(|&arg| bind_of(facts, arg)).collect();
    if let Some(chosen) = resolve(f, facts, b, &binds) {
        return Some(Step::One(chosen));
    }
    // A comparison, or a choice between constants on one, that the block
    // chooses on: the two answers are worked out, one for each of its two
    // values.
    if !may_split || !matches!(block.end, End::Switch { .. }) {
        return None;
    }
    let mut on: Option<Value> = None;
    let mut picks: Vec<Option<(u64, u64)>> = Vec::new();
    for &arg in args {
        let Arg::Value(v) = arg else {
            picks.push(None);
            continue;
        };
        let (x, pick) = if facts.compared[v.0] {
            (v, None)
        } else if let Some((x, t, o)) = facts.chosen[v.0]
            && facts.compared[x.0]
            && let (Some(t), Some(o)) = (facts.constant[t.0], facts.constant[o.0])
        {
            (x, Some((t, o)))
        } else {
            picks.push(None);
            continue;
        };
        if on.is_some_and(|y| y != x) {
            return None;
        }
        on = Some(x);
        picks.push(Some(pick.unwrap_or((1, 0))));
    }
    let on = on?;
    let mut jumps = Vec::new();
    for case in [0u64, 1] {
        let binds: Vec<Bind> = args
            .iter()
            .zip(&picks)
            .map(|(&arg, pick)| match pick {
                Some((t, o)) => Bind {
                    known: Some(if case != 0 { *t } else { *o }),
                    value: None,
                },
                None => bind_of(facts, arg),
            })
            .collect();
        jumps.push(resolve(f, facts, b, &binds)?);
    }
    Some(Step::Split { on, jumps })
}

/// The jump that block `b` makes when it is given these, as the block it goes
/// to and what it gives, if the block ends with a jump or a choice on a known
/// value, and every value it gives is known or is in the block that jumps.
fn resolve(f: &Function, facts: &Facts, b: usize, binds: &[Bind]) -> Option<(usize, Vec<Arg>)> {
    let mut env = facts.scratch.take();
    let chosen = resolved(f, facts, b, binds, &mut env);
    env.clear();
    facts.scratch.replace(env);
    chosen
}

fn resolved(
    f: &Function,
    facts: &Facts,
    b: usize,
    binds: &[Bind],
    env: &mut Env,
) -> Option<(usize, Vec<Arg>)> {
    let block = &f.blocks[b];
    crate::work::gone_through(block.params.len() + block.instructions.len());
    for (&p, &bind) in block.params.iter().zip(binds) {
        env.insert(p, bind);
    }
    let known = |env: &Env, v: Value| match env.get(v) {
        Some(bind) => bind.known,
        None => facts.constant[v.0],
    };
    for i in &block.instructions {
        let [r] = i.results[..] else {
            unreachable!("a block that is gone through writes nothing and calls nothing")
        };
        let ty = f.types[r.0];
        let answer = match &i.op {
            Op::Constant(Constant::Int(c)) => Some(c & mask(bits_of(ty))),
            Op::Binary { op, a, b } => match (known(env, *a), known(env, *b)) {
                (Some(x), Some(y)) if matches!(ty, Type::Int(_)) => {
                    Some(binary(*op, bits_of(ty), x, y))
                }
                _ => None,
            },
            Op::Compare { op, a, b } => match (known(env, *a), known(env, *b)) {
                (Some(x), Some(y)) => Some(compare(*op, bits_of(f.types[a.0]), x, y)),
                _ => None,
            },
            Op::Widen { signed, value } => known(env, *value)
                .map(|n| widen(*signed, bits_of(f.types[value.0]), bits_of(ty), n)),
            Op::Narrow(value) => known(env, *value).map(|n| narrow(bits_of(ty), n)),
            Op::Choose {
                on,
                then,
                otherwise,
            } => known(env, *on).and_then(|c| known(env, if c != 0 { *then } else { *otherwise })),
            _ => None,
        };
        env.insert(
            r,
            Bind {
                known: answer,
                value: None,
            },
        );
    }
    let chosen = match &block.end {
        End::Jump(j) => j,
        End::Switch { on, jumps } => jumps.get(known(env, *on)? as usize)?,
        End::Return(_) => return None,
    };
    let mut args = Vec::with_capacity(chosen.args.len());
    for &v in &chosen.args {
        args.push(match env.get(v) {
            None => Arg::Value(v),
            Some(Bind { value: Some(v), .. }) => Arg::Value(*v),
            Some(Bind { known: Some(c), .. }) => Arg::Const(*c, f.types[v.0]),
            Some(_) => return None,
        });
    }
    Some((chosen.block, args))
}

/// The jump, with a constant made in block `a` for each number that the jump
/// gives and no value stands for.
fn make(
    f: &mut Function,
    facts: &mut Facts,
    extra: &mut Vec<Instruction>,
    (block, args): (usize, Vec<Arg>),
) -> Jump {
    let args = args
        .into_iter()
        .map(|arg| match arg {
            Arg::Value(v) => v,
            Arg::Const(c, ty) => {
                let q = Value(f.types.len());
                f.types.push(ty);
                facts.constant.push(Some(c));
                facts.compared.push(false);
                facts.chosen.push(None);
                extra.push(Instruction {
                    results: vec![q],
                    op: Op::Constant(Constant::Int(c)),
                    at: Place::NONE,
                });
                q
            }
        })
        .collect();
    Jump { block, args }
}
