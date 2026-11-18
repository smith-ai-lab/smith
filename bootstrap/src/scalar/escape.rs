//! Which of a function's values can be, or hold, memory that the function
//! gives back as its result.
//!
//! The handler of `alloc` builds a function's result in its caller's memory
//! and its other lists and texts in memory it gives up when it returns. Which
//! is which is decided here, for each value, from the function alone.
//!
//! Two values are put in one group when memory that one is, or holds, can be
//! reached through the other: an address and the address moved from it, an
//! address and the addresses held at it, a value and what a block it jumps to
//! calls it, and every address a call takes or gives. A group is *given back*
//! when it has an address the function returns. The test errs towards giving
//! back: it never says a value stays when it may not, and a word of 8 bytes
//! that is no address is taken for one only where it moves through memory,
//! which is how a copy of a list's elements moves an address. A value that
//! holds no address is never in a group, and neither is a constant, which
//! names memory but is none the function made.

use super::{Bits, Constant, End, Function, Op, Type, Value};

/// For each value of the function, whether it is in a group that the function
/// gives back through an address it returns.
pub fn given_back(f: &Function) -> Vec<bool> {
    let mut groups = Groups::new(f.types.len());
    let holds = |v: &Value| f.types[v.0] == Type::Address || f.types[v.0] == Type::Int(Bits::B64);
    let mut constant = vec![false; f.types.len()];
    for i in f.blocks.iter().flat_map(|b| &b.instructions) {
        if let Op::Constant(Constant::Int(_) | Constant::Null | Constant::Data(_)) = i.op {
            constant[i.results[0].0] = true;
        }
    }
    let mut join = |a: Value, b: Value| {
        if !constant[a.0] && !constant[b.0] {
            groups.join(a.0, b.0);
        }
    };
    for block in &f.blocks {
        for i in &block.instructions {
            match &i.op {
                Op::Binary { a, .. } if f.types[i.results[0].0] == Type::Address => {
                    join(i.results[0], *a)
                }
                Op::Choose {
                    then, otherwise, ..
                } if holds(&i.results[0]) => {
                    join(i.results[0], *then);
                    join(i.results[0], *otherwise);
                }
                Op::Load { address, .. } if holds(&i.results[0]) => join(i.results[0], *address),
                Op::Store { address, value, .. } if holds(value) => join(*value, *address),
                Op::Call { args, .. } => {
                    let held: Vec<Value> = (args.iter().chain(&i.results))
                        .filter(|v| f.types[v.0] == Type::Address)
                        .copied()
                        .collect();
                    for v in held.iter().skip(1) {
                        join(held[0], *v);
                    }
                }
                _ => {}
            }
        }
        let jumps = match &block.end {
            End::Jump(jump) => vec![jump],
            End::Switch { jumps, .. } => jumps.iter().collect(),
            End::Return(_) => Vec::new(),
        };
        for jump in jumps {
            for (arg, param) in jump.args.iter().zip(&f.blocks[jump.block].params) {
                if holds(param) {
                    join(*arg, *param);
                }
            }
        }
    }
    let mut back = vec![false; f.types.len()];
    for block in &f.blocks {
        if let End::Return(values) = &block.end {
            for v in values.iter().filter(|v| f.types[v.0] == Type::Address) {
                back[groups.find(v.0)] = true;
            }
        }
    }
    (0..f.types.len())
        .map(|v| !constant[v] && back[groups.find(v)])
        .collect()
}

/// Groups of values, each by the value that stands for it.
struct Groups(Vec<usize>);

impl Groups {
    fn new(n: usize) -> Groups {
        Groups((0..n).collect())
    }

    fn find(&mut self, mut v: usize) -> usize {
        while self.0[v] != v {
            self.0[v] = self.0[self.0[v]];
            v = self.0[v];
        }
        v
    }

    fn join(&mut self, a: usize, b: usize) {
        let (a, b) = (self.find(a), self.find(b));
        self.0[a] = b;
    }
}
