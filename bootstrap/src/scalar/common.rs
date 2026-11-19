//! The fourth pass over the scalar form: what is worked out twice is worked
//! out once.
//!
//! One walk over each function's tree of dominators, which says for each
//! block the blocks that every path to it passes through. An instruction that
//! reads no memory, writes none, and calls nothing is looked up, with the
//! type it gives and the values it reads, among those of the blocks that
//! dominate it and of the blocks before it in its own. When one is found,
//! the instruction is taken away and its value is read as the first one's. A
//! value that was replaced is never looked up, so an instruction whose inputs
//! were replaced is found too, in the same walk. A read through an address is
//! never replaced: the form does not say what a write may change.
//!
//! The dominators are found as Cooper, Harvey, and Kennedy do, which takes
//! one walk over the blocks in order, and one more to see that nothing
//! changed, when the jumps back only go to blocks that dominate the jump, as
//! those of a loop do.

use std::collections::HashMap;

use super::eval::{bits_of, float_mask, mask};
use super::graph::{self, jumps};
use super::{Constant, Function, Op, Program, Type, Value};

/// The program with each instruction that does what an earlier one does, to
/// the same values, replaced by the earlier one's value.
pub fn common(program: &Program) -> Program {
    Program {
        data: program.data.clone(),
        functions: program.functions.iter().map(shared).collect(),
        main: program.main,
    }
}

/// Whether the instruction gives one value that depends only on the values
/// it reads.
fn pure(op: &Op) -> bool {
    matches!(
        op,
        Op::Constant(_)
            | Op::Binary { .. }
            | Op::Compare { .. }
            | Op::FloatBinary { .. }
            | Op::FloatNegate(_)
            | Op::FloatCompare { .. }
            | Op::FloatFromInt { .. }
            | Op::IntFromFloat { .. }
            | Op::FloatResize(_)
            | Op::VectorBinary { .. }
            | Op::VectorFloatBinary { .. }
            | Op::VectorCompare { .. }
            | Op::VectorFloatCompare { .. }
            | Op::VectorChoose { .. }
            | Op::VectorSplat(_)
            | Op::VectorExtract { .. }
            | Op::VectorInsert { .. }
            | Op::VectorSum(_)
            | Op::VectorFirstSet(_)
            | Op::Widen { .. }
            | Op::Narrow(_)
            | Op::Choose { .. }
    )
}

fn shared(f: &Function) -> Function {
    let mut f = f.clone();
    let children = dominated(&f);
    let mut sub: Vec<Value> = (0..f.types.len()).map(Value).collect();
    let mut first: HashMap<(Op, Type), Value> = HashMap::new();
    enum Step {
        Enter(usize),
        Leave(Vec<(Op, Type)>),
    }
    let mut stack = vec![Step::Enter(0)];
    while let Some(step) = stack.pop() {
        match step {
            Step::Leave(added) => {
                for key in added {
                    first.remove(&key);
                }
            }
            Step::Enter(b) => {
                let mut added = Vec::new();
                for instruction in &f.blocks[b].instructions {
                    let [result] = instruction.results[..] else {
                        continue;
                    };
                    if !pure(&instruction.op) {
                        continue;
                    }
                    let ty = f.types[result.0];
                    let mut op = super::inline::renamed(&instruction.op, &|v| sub[v.0]);
                    match (&mut op, ty) {
                        (Op::Constant(Constant::Int(n)), Type::Int(_)) => *n &= mask(bits_of(ty)),
                        (Op::Constant(Constant::Float(n)), Type::Float(bits)) => {
                            *n &= float_mask(bits)
                        }
                        _ => {}
                    }
                    match first.get(&(op.clone(), ty)) {
                        Some(&earlier) => sub[result.0] = earlier,
                        None => {
                            first.insert((op.clone(), ty), result);
                            added.push((op, ty));
                        }
                    }
                }
                stack.push(Step::Leave(added));
                stack.extend(children[b].iter().rev().map(|&c| Step::Enter(c)));
            }
        }
    }
    graph::rename(&mut f, &|v| sub[v.0]);
    for block in &mut f.blocks {
        block
            .instructions
            .retain(|i| i.results.iter().all(|r| sub[r.0] == *r));
    }
    f
}

/// For each block, the blocks it immediately dominates, in the order the
/// blocks are walked. A block that nothing reaches has none and is in none.
pub(super) fn dominated(f: &Function) -> Vec<Vec<usize>> {
    let order = graph::reverse_postorder(f);
    let n = f.blocks.len();
    let mut at = vec![usize::MAX; n];
    for (place, &b) in order.iter().enumerate() {
        at[b] = place;
    }
    let mut into: Vec<Vec<usize>> = vec![Vec::new(); n];
    for &b in &order {
        for j in jumps(&f.blocks[b].end) {
            into[j.block].push(b);
        }
    }
    let mut idom = vec![usize::MAX; n];
    idom[0] = 0;
    let meet = |idom: &[usize], mut a: usize, mut b: usize| {
        while a != b {
            while at[a] > at[b] {
                a = idom[a];
            }
            while at[b] > at[a] {
                b = idom[b];
            }
        }
        a
    };
    let mut changed = true;
    while changed {
        changed = false;
        for &b in &order[1..] {
            let mut done = into[b].iter().copied().filter(|&p| idom[p] != usize::MAX);
            let first = done.next().expect("a block that is reached has a way in");
            let new = done.fold(first, |a, p| meet(&idom, a, p));
            if idom[b] != new {
                idom[b] = new;
                changed = true;
            }
        }
    }
    let mut children = vec![Vec::new(); n];
    for &b in &order[1..] {
        children[idom[b]].push(b);
    }
    children
}
