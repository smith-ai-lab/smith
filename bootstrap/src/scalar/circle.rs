//! The pass that makes one function of functions that call each other, so
//! that the pass that gives a function that calls itself a stack of its own
//! gives a circle of functions one.
//!
//! The circle is a set of functions each of which can reach each of the others
//! by calls. The one function chooses by a number which of them it is, takes
//! the parameters of all of them, and returns the results of all of them, of
//! which a caller reads the ones of the function it called. A call of a
//! function of the circle from a function of the circle is a call of the one
//! function, with the number of the callee and the values the other functions
//! do not read made up. Each function of the circle is left as a function of
//! its own that calls the one function, so that no call from outside it
//! changes.
//!
//! A function that is in no circle is not changed, and neither is `main`.

use super::graph::rename_end;
use super::{
    Bits, Block, Constant, End, Function, Instruction, Jump, Op, Place, Program, Type, Value,
};

/// The program with each circle of functions made one function, which calls
/// itself and which each function of the circle calls.
pub fn merge(program: Program) -> Program {
    let circles = circles(&program);
    if circles.is_empty() {
        return program;
    }
    let Program {
        data,
        mut functions,
        main,
    } = program;
    for members in circles {
        let one = functions.len();
        let (merged, wrappers) = combine(&functions, &members, one);
        for (&m, wrapper) in members.iter().zip(wrappers) {
            functions[m] = wrapper;
        }
        functions.push(merged);
    }
    Program {
        data,
        functions,
        main,
    }
}

/// The circles of the program: the sets of two functions or more that each
/// call the others, directly or through others. Each is in order of the
/// functions' places, and `main` is in none.
fn circles(program: &Program) -> Vec<Vec<usize>> {
    let n = program.functions.len();
    let mut calls: Vec<Vec<usize>> = vec![Vec::new(); n];
    for (i, f) in program.functions.iter().enumerate() {
        if i == program.main {
            continue;
        }
        for block in &f.blocks {
            for instruction in &block.instructions {
                if let Op::Call { function, .. } = instruction.op
                    && function != program.main
                    && function != i
                {
                    calls[i].push(function);
                }
            }
        }
    }
    if calls.iter().all(Vec::is_empty) {
        return Vec::new();
    }
    // The order in which the walk of the calls leaves each function.
    let mut order = Vec::with_capacity(n);
    let mut seen = vec![false; n];
    for start in 0..n {
        if seen[start] {
            continue;
        }
        seen[start] = true;
        let mut stack = vec![(start, 0usize)];
        while let Some((f, k)) = stack.last_mut() {
            if let Some(&g) = calls[*f].get(*k) {
                *k += 1;
                if !seen[g] {
                    seen[g] = true;
                    stack.push((g, 0));
                }
            } else {
                order.push(*f);
                stack.pop();
            }
        }
    }
    // Walking the calls the other way from the last function left finds one
    // circle at a time.
    let mut called_by: Vec<Vec<usize>> = vec![Vec::new(); n];
    for (f, targets) in calls.iter().enumerate() {
        for &g in targets {
            called_by[g].push(f);
        }
    }
    let mut taken = vec![false; n];
    let mut found = Vec::new();
    for &start in order.iter().rev() {
        if taken[start] {
            continue;
        }
        taken[start] = true;
        let mut members = vec![start];
        let mut stack = vec![start];
        while let Some(f) = stack.pop() {
            for &g in &called_by[f] {
                if !taken[g] {
                    taken[g] = true;
                    members.push(g);
                    stack.push(g);
                }
            }
        }
        if members.len() > 1 {
            members.sort_unstable();
            found.push(members);
        }
    }
    found.sort();
    found
}

/// Instructions being made in a function whose values are `types`.
struct Made<'a> {
    types: &'a mut Vec<Type>,
    out: Vec<Instruction>,
}

impl Made<'_> {
    fn fresh(&mut self, ty: Type) -> Value {
        self.types.push(ty);
        Value(self.types.len() - 1)
    }

    fn constant(&mut self, constant: Constant, ty: Type) -> Value {
        let result = self.fresh(ty);
        self.out.push(Instruction {
            results: vec![result],
            op: Op::Constant(constant),
            at: Place::NONE,
        });
        result
    }

    /// A value of type `ty` that nothing reads.
    fn dummy(&mut self, ty: Type) -> Value {
        let constant = match ty {
            Type::Int(_) => Constant::Int(0),
            Type::Float(_) => Constant::Float(0),
            Type::Vector(_) => Constant::Vector(0, 0),
            Type::Address => Constant::Null,
        };
        self.constant(constant, ty)
    }

    /// The number of a function of the circle, which is how the one function
    /// chooses.
    fn which(&mut self, k: usize) -> Value {
        self.constant(Constant::Int(k as u64), Type::Int(Bits::B64))
    }
}

/// The one function made of `members`, whose place will be `one`, and the
/// function each member becomes in place of what it was.
fn combine(functions: &[Function], members: &[usize], one: usize) -> (Function, Vec<Function>) {
    let entry_types = |m: usize| -> Vec<Type> {
        let f = &functions[m];
        f.blocks[0].params.iter().map(|p| f.types[p.0]).collect()
    };
    let mut types = vec![Type::Int(Bits::B64)];
    let selector = Value(0);
    // The parameters of the one function: the selector, and then those of each
    // member.
    let mut params = vec![selector];
    let mut entry: Vec<Vec<Value>> = Vec::new();
    for &m in members {
        let own: Vec<Value> = entry_types(m)
            .into_iter()
            .map(|ty| {
                types.push(ty);
                Value(types.len() - 1)
            })
            .collect();
        params.extend(&own);
        entry.push(own);
    }
    let results: Vec<Type> = members
        .iter()
        .flat_map(|&m| functions[m].results.iter().copied())
        .collect();
    // Where the values and the blocks of each member are in the one function.
    let mut value_base = Vec::new();
    for &m in members {
        value_base.push(types.len());
        types.extend(&functions[m].types);
    }
    let mut block_base = Vec::new();
    let mut next = 1;
    for &m in members {
        block_base.push(next);
        next += functions[m].blocks.len();
    }
    let mut blocks = vec![Block {
        params,
        instructions: Vec::new(),
        end: End::Switch {
            on: selector,
            jumps: members
                .iter()
                .enumerate()
                .map(|(k, _)| Jump {
                    block: block_base[k],
                    args: entry[k].clone(),
                })
                .collect(),
        },
    }];
    for (k, &m) in members.iter().enumerate() {
        let (by, to) = (value_base[k], block_base[k]);
        let shifted = |v: Value| Value(v.0 + by);
        for block in &functions[m].blocks {
            let mut made = Made {
                types: &mut types,
                out: Vec::new(),
            };
            for instruction in &block.instructions {
                let results: Vec<Value> = instruction.results.iter().map(|&r| shifted(r)).collect();
                let mut op = instruction.op.clone();
                op.rename(&shifted);
                if let Op::Call { function, args } = &op
                    && let Some(j) = members.iter().position(|&x| x == *function)
                {
                    // A call of a member is a call of the one function.
                    let mut all = vec![made.which(j)];
                    let mut answers = Vec::new();
                    for (other, &o) in members.iter().enumerate() {
                        if other == j {
                            all.extend(args);
                            answers.extend(&results);
                        } else {
                            for ty in entry_types(o) {
                                all.push(made.dummy(ty));
                            }
                            for &ty in &functions[o].results {
                                answers.push(made.fresh(ty));
                            }
                        }
                    }
                    made.out.push(Instruction {
                        results: answers,
                        op: Op::Call {
                            function: one,
                            args: all,
                        },
                        at: instruction.at,
                    });
                    continue;
                }
                made.out.push(Instruction {
                    results,
                    op,
                    at: instruction.at,
                });
            }
            let mut end = block.end.clone();
            rename_end(&mut end, &shifted);
            for jump in end.jumps_mut() {
                jump.block += to;
            }
            if let End::Return(values) = &mut end {
                let mut all = Vec::new();
                for (other, &o) in members.iter().enumerate() {
                    if other == k {
                        all.extend(values.iter().copied());
                    } else {
                        for &ty in &functions[o].results {
                            all.push(made.dummy(ty));
                        }
                    }
                }
                *values = all;
            }
            let instructions = made.out;
            blocks.push(Block {
                params: block.params.iter().map(|&p| shifted(p)).collect(),
                instructions,
                end,
            });
        }
    }
    let name = (members.iter())
        .map(|&m| functions[m].name.as_str())
        .collect::<Vec<_>>()
        .join(" + ");
    let merged = Function {
        types,
        results,
        blocks,
        cold: false,
        name,
    };
    // Each member calls the one function with its own number.
    let wrappers = members
        .iter()
        .enumerate()
        .map(|(k, &m)| {
            let f = &functions[m];
            let mut types = Vec::new();
            let mut made_params = Vec::new();
            for ty in entry_types(m) {
                types.push(ty);
                made_params.push(Value(types.len() - 1));
            }
            let mut made = Made {
                types: &mut types,
                out: Vec::new(),
            };
            let mut args = vec![made.which(k)];
            let mut answers = Vec::new();
            let mut mine = Vec::new();
            for (other, &o) in members.iter().enumerate() {
                if other == k {
                    args.extend(&made_params);
                } else {
                    for ty in entry_types(o) {
                        args.push(made.dummy(ty));
                    }
                }
                for &ty in &functions[o].results {
                    let value = made.fresh(ty);
                    answers.push(value);
                    if other == k {
                        mine.push(value);
                    }
                }
            }
            made.out.push(Instruction {
                results: answers,
                op: Op::Call {
                    function: one,
                    args,
                },
                at: Place::NONE,
            });
            let instructions = made.out;
            Function {
                types,
                results: f.results.clone(),
                blocks: vec![Block {
                    params: made_params,
                    instructions,
                    end: End::Return(mine),
                }],
                cold: f.cold,
                name: f.name.clone(),
            }
        })
        .collect();
    (merged, wrappers)
}
