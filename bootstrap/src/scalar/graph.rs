//! What the passes after inlining share: the order of a function's blocks,
//! the jumps that end them, and the values that a block's parameters only
//! copy.

use super::{End, Function, Jump, Value};

/// The jumps a block's end makes, in order.
pub(super) fn jumps(end: &End) -> &[Jump] {
    end.jumps()
}

pub(super) fn jumps_mut(end: &mut End) -> &mut [Jump] {
    end.jumps_mut()
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
        if let Some(to) = f.blocks[b].end.jumps().get(*next).map(|j| j.block) {
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
    // The jumps into each block, as the block that jumps and the place of the
    // jump among its jumps, and where each value is given as the argument of a
    // parameter, as the block and the place of the parameter.
    let each_jump = |each: &mut dyn FnMut(usize, usize, usize, &Jump)| {
        for (b, block) in f.blocks.iter().enumerate().filter(|(b, _)| reached[*b]) {
            for (at, j) in jumps(&block.end).iter().enumerate() {
                each(b, at, j.block, j);
            }
        }
    };
    let into = Lists::of(f.blocks.len(), (0, 0), |put| {
        each_jump(&mut |b, at, to, _| put(to, (b, at)));
    });
    let given_to = Lists::of(f.types.len(), (0, 0), |put| {
        each_jump(&mut |_, _, to, j| {
            for (k, v) in j.args.iter().enumerate() {
                put(v.0, (to, k));
            }
        });
    });
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
        for &(from, at) in into.of_key(b) {
            let a = find(&mut copy, jumps(&f.blocks[from].end)[at].args[k]);
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
            work.extend(
                given_to
                    .of_key(p.0)
                    .iter()
                    .rev()
                    .copied()
                    .filter(|(b, _)| *b != 0),
            );
        }
    }
    for v in 0..copy.len() {
        copy[v] = find(&mut copy, Value(v));
    }
    copy
}

/// Lists of pairs, one list for each key, held in one buffer, so that a walk
/// that makes a list for each block or value asks for two buffers and not a
/// list for each key. The order of a list is the order the pairs are given.
struct Lists {
    start: Vec<usize>,
    items: Vec<(usize, usize)>,
}

impl Lists {
    /// The lists of `keys` keys that `give` puts pairs in: it is called twice,
    /// to count and then to fill, and `fill` is what the buffer holds before.
    fn of(
        keys: usize,
        fill: (usize, usize),
        give: impl Fn(&mut dyn FnMut(usize, (usize, usize))),
    ) -> Lists {
        let mut start = vec![0usize; keys + 1];
        give(&mut |key, _| start[key + 1] += 1);
        for key in 0..keys {
            start[key + 1] += start[key];
        }
        let mut items = vec![fill; start[keys]];
        let mut next = start.clone();
        give(&mut |key, pair| {
            items[next[key]] = pair;
            next[key] += 1;
        });
        Lists { start, items }
    }

    /// The pairs of the key, in the order they were given.
    fn of_key(&self, key: usize) -> &[(usize, usize)] {
        &self.items[self.start[key]..self.start[key + 1]]
    }
}
