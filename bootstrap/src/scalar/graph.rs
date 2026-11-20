//! What the passes after inlining share: the order of a function's blocks,
//! the jumps that end them, and the values that a block's parameters only
//! copy.

use super::{End, Function, Jump, Value};

/// The jumps a block's end makes, in order.
pub(super) fn jumps(end: &End) -> Vec<&Jump> {
    match end {
        End::Jump(j) => vec![j],
        End::Switch { jumps, .. } => jumps.iter().collect(),
        End::Return(_) => Vec::new(),
    }
}

pub(super) fn jumps_mut(end: &mut End) -> Vec<&mut Jump> {
    match end {
        End::Jump(j) => vec![j],
        End::Switch { jumps, .. } => jumps.iter_mut().collect(),
        End::Return(_) => Vec::new(),
    }
}

/// The blocks reachable from the first, each before every block it reaches
/// except through a jump back to a block already started.
pub(super) fn reverse_postorder(f: &Function) -> Vec<usize> {
    let mut seen = vec![false; f.blocks.len()];
    let mut order = Vec::new();
    seen[0] = true;
    let mut stack = vec![(0, 0)];
    while let Some((b, next)) = stack.last_mut() {
        let b = *b;
        let successors = f.blocks[b].end.successors();
        if let Some(&to) = successors.get(*next) {
            *next += 1;
            if !seen[to] {
                seen[to] = true;
                stack.push((to, 0));
            }
        } else {
            order.push(b);
            stack.pop();
        }
    }
    order.reverse();
    order
}

/// The function with every value read through `by`.
pub(super) fn rename(f: &mut Function, by: &impl Fn(Value) -> Value) {
    for block in &mut f.blocks {
        for i in &mut block.instructions {
            i.op.rename(by);
        }
        rename_end(&mut block.end, by);
    }
}

pub(super) fn rename_end(end: &mut End, by: &impl Fn(Value) -> Value) {
    match end {
        End::Return(values) => values.iter_mut().for_each(|v| *v = by(*v)),
        End::Switch { on, .. } => *on = by(*on),
        End::Jump(_) => {}
    }
    for j in jumps_mut(end) {
        j.args.iter_mut().for_each(|v| *v = by(*v));
    }
}

/// For each value, the value it is a copy of, which is itself unless it is a
/// parameter that every jump into its block gives the same value, not
/// counting the parameter itself. A parameter of the first block is the
/// function's own, so it is never a copy. Blocks that nothing reaches are
/// not looked at.
///
/// A parameter that stands for another is looked at again where it is given
/// on, so a value copied through several blocks is found by the walk that
/// finds the first copy.
pub(super) fn copies(f: &Function) -> Vec<Value> {
    let reached: Vec<bool> = {
        let mut r = vec![false; f.blocks.len()];
        for b in reverse_postorder(f) {
            r[b] = true;
        }
        r
    };
    // The jumps into each block, and where each value is given as the
    // argument of a parameter.
    let mut into: Vec<Vec<&Jump>> = vec![Vec::new(); f.blocks.len()];
    let mut given_to: Vec<Vec<(usize, usize)>> = vec![Vec::new(); f.types.len()];
    for (_, block) in f.blocks.iter().enumerate().filter(|(b, _)| reached[*b]) {
        for j in jumps(&block.end) {
            into[j.block].push(j);
            for (k, v) in j.args.iter().enumerate() {
                given_to[v.0].push((j.block, k));
            }
        }
    }
    let mut copy: Vec<Value> = (0..f.types.len()).map(Value).collect();
    fn find(copy: &mut [Value], v: Value) -> Value {
        let mut root = v;
        while copy[root.0] != root {
            root = copy[root.0];
        }
        let mut at = v;
        while copy[at.0] != root {
            let next = copy[at.0];
            copy[at.0] = root;
            at = next;
        }
        root
    }
    let mut work: Vec<(usize, usize)> = Vec::new();
    for b in (1..f.blocks.len()).rev().filter(|b| reached[*b]) {
        for k in (0..f.blocks[b].params.len()).rev() {
            work.push((b, k));
        }
    }
    while let Some((b, k)) = work.pop() {
        let p = f.blocks[b].params[k];
        if copy[p.0] != p {
            continue;
        }
        let mut same: Option<Value> = None;
        let mut one = true;
        for j in &into[b] {
            let a = find(&mut copy, j.args[k]);
            if a == p {
                continue;
            }
            match same {
                None => same = Some(a),
                Some(s) if s == a => {}
                Some(_) => {
                    one = false;
                    break;
                }
            }
        }
        if let (true, Some(v)) = (one, same) {
            copy[p.0] = v;
            work.extend(given_to[p.0].iter().rev().copied().filter(|(b, _)| *b != 0));
        }
    }
    for v in 0..copy.len() {
        copy[v] = find(&mut copy, Value(v));
    }
    copy
}
