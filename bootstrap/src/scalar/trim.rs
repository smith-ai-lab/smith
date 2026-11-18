//! The second pass over the scalar form, after inlining: a function is passed only
//! the values it reads, and gives back only the values it makes.
//!
//! A parameter of a function that nothing reads is taken away, and with it
//! the argument of each call. A result that is the same one of the function's
//! parameters on every return is taken away, and each call's reader is given
//! the argument in its place. Functions are taken callees first, so what a
//! call leaves out is known when the function that makes it is looked at, and
//! a parameter that a function only hands on to a call that does not read it
//! is not read. The pass is one walk over the program and repeats nothing
//! until it stops changing. `main`, which the system starts, keeps its
//! parameters, and a function in a ring of calls, which the form does not have
//! yet, is left as it is, and so is what calls it.

use super::graph::{self, jumps};
use super::inline::calls;
use super::remove::{drop_unreached, drop_unused};
use super::{End, Function, Op, Program, Value};

/// What the pass did to a function's parameters and results, by their places
/// before it.
#[derive(Clone)]
struct Trimmed {
    /// Whether each parameter stays.
    params: Vec<bool>,
    /// For each result, the parameter it is on every return, or `None` when it
    /// stays.
    results: Vec<Option<usize>>,
}

impl Trimmed {
    fn same(params: usize, results: usize) -> Trimmed {
        Trimmed {
            params: vec![true; params],
            results: vec![None; results],
        }
    }

    fn changes(&self) -> bool {
        self.params.iter().any(|p| !p) || self.results.iter().any(|r| r.is_some())
    }
}

/// The program with every function passed what it reads and giving back what
/// it makes.
pub fn trim(program: &Program) -> Program {
    let mut trimmed: Vec<Trimmed> = (program.functions.iter())
        .map(|f| Trimmed::same(f.blocks[0].params.len(), f.results.len()))
        .collect();
    let mut functions = program.functions.clone();
    for component in components(program) {
        let ring = component.len() > 1 || calls(&functions[component[0]]).contains(&component[0]);
        for f in component {
            rewrite_calls(&mut functions[f], &trimmed);
            if !ring && f != program.main {
                trimmed[f] = trim_function(&mut functions[f]);
            }
        }
    }
    Program {
        data: program.data.clone(),
        functions,
        main: program.main,
    }
}

/// Takes away the results and the parameters of the function that are not
/// needed, and says which.
fn trim_function(f: &mut Function) -> Trimmed {
    drop_unreached(f);
    let copy = graph::copies(f);
    graph::rename(f, &|v| copy[v.0]);
    let mut trimmed = Trimmed::same(f.blocks[0].params.len(), f.results.len());
    let own = f.blocks[0].params.clone();
    // A first block that is jumped to is a loop's, and its parameters are
    // not the function's own after the first round.
    let entered = f
        .blocks
        .iter()
        .any(|b| jumps(&b.end).iter().any(|j| j.block == 0));
    let mut returns = f.blocks.iter().filter_map(|b| match &b.end {
        End::Return(values) => Some(values),
        _ => None,
    });
    if !entered && let Some(first) = returns.next() {
        let rest: Vec<_> = returns.collect();
        for (k, v) in first.iter().enumerate() {
            let place = own.iter().position(|p| p == v);
            if place.is_some() && rest.iter().all(|r| r[k] == *v) {
                trimmed.results[k] = place;
            }
        }
    }
    if trimmed.results.iter().any(|r| r.is_some()) {
        let keep: Vec<bool> = trimmed.results.iter().map(|r| r.is_none()).collect();
        let by_keep = |values: &mut Vec<Value>| {
            let mut k = 0;
            values.retain(|_| {
                k += 1;
                keep[k - 1]
            });
        };
        for block in &mut f.blocks {
            if let End::Return(values) = &mut block.end {
                by_keep(values);
            }
        }
        let mut k = 0;
        f.results.retain(|_| {
            k += 1;
            keep[k - 1]
        });
    }
    drop_unused(f, false);
    trimmed.params = own.iter().map(|p| f.blocks[0].params.contains(p)).collect();
    trimmed
}

/// Leaves out of each call the arguments the callee no longer takes, and
/// reads each result the callee no longer gives as the argument it is.
fn rewrite_calls(f: &mut Function, trimmed: &[Trimmed]) {
    let mut by: Vec<Value> = (0..f.types.len()).map(Value).collect();
    let mut any = false;
    for block in &mut f.blocks {
        for instruction in &mut block.instructions {
            let Op::Call { function, args } = &mut instruction.op else {
                continue;
            };
            let t = &trimmed[*function];
            if !t.changes() {
                continue;
            }
            any = true;
            for (r, place) in instruction.results.iter().zip(&t.results) {
                if let Some(p) = place {
                    by[r.0] = args[*p];
                }
            }
            let mut k = 0;
            args.retain(|_| {
                k += 1;
                t.params[k - 1]
            });
            let mut k = 0;
            instruction.results.retain(|_| {
                k += 1;
                t.results[k - 1].is_none()
            });
        }
    }
    if any {
        let find = |mut v: Value| {
            while by[v.0] != v {
                v = by[v.0];
            }
            v
        };
        graph::rename(f, &find);
    }
}

/// The groups of functions that call one another round, each after the
/// groups it calls, taken from `main` and then from every function that is
/// left.
fn components(program: &Program) -> Vec<Vec<usize>> {
    let n = program.functions.len();
    let callees: Vec<Vec<usize>> = program.functions.iter().map(calls).collect();
    let mut index = vec![usize::MAX; n];
    let mut low = vec![0; n];
    let mut on_stack = vec![false; n];
    let mut stack: Vec<usize> = Vec::new();
    let mut found: Vec<Vec<usize>> = Vec::new();
    let mut next = 0;
    let roots = std::iter::once(program.main).chain(0..n);
    for root in roots {
        if index[root] != usize::MAX {
            continue;
        }
        let mut work = vec![(root, 0)];
        index[root] = next;
        low[root] = next;
        next += 1;
        stack.push(root);
        on_stack[root] = true;
        while let Some(&mut (f, ref mut at)) = work.last_mut() {
            if let Some(&g) = callees[f].get(*at) {
                *at += 1;
                if index[g] == usize::MAX {
                    index[g] = next;
                    low[g] = next;
                    next += 1;
                    stack.push(g);
                    on_stack[g] = true;
                    work.push((g, 0));
                } else if on_stack[g] {
                    low[f] = low[f].min(index[g]);
                }
            } else {
                work.pop();
                if let Some(&(parent, _)) = work.last() {
                    low[parent] = low[parent].min(low[f]);
                }
                if low[f] == index[f] {
                    let mut group = Vec::new();
                    loop {
                        let g = stack.pop().unwrap();
                        on_stack[g] = false;
                        group.push(g);
                        if g == f {
                            break;
                        }
                    }
                    group.reverse();
                    found.push(group);
                }
            }
        }
    }
    found
}
