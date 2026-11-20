//! The pass that gives a function that calls itself, where the call is not the
//! last thing it does, a stack of its own: the calls are not calls of the
//! system's stack but a loop, and what each call keeps while the one it made
//! runs is in memory that grows.
//!
//! The function is cut at each call of itself, so that each piece ends with the
//! call or with what the block ended with. A value that a piece uses and an
//! earlier piece made is given to it as a parameter by every jump that enters
//! it, as it is when the piece is entered after a call. A call is then a push
//! of the values that are alive when the call returns, a number that says
//! where, and a jump to the start of the function, which is the loop. A return
//! is a pop: the number says where to go on, and the values come back. The
//! stack is a run of chunks, each asked of the system, the first when the
//! function is entered and the next when the one in use has no room, and a
//! frame that says where the chunk before it was is what a pop finds when a
//! chunk is empty. Two values go from block to block with the rest: where the
//! free memory in the chunk begins, and how many bytes of it are left, which
//! is how the handler of `alloc` holds a stack, so `install`, which runs
//! after, finds the requests for a chunk as it finds any request and gives
//! them room in a stack of memory that is released when the function returns.
//!
//! A function that does not call itself, or only calls itself as the last thing
//! it does, which `tail` has made a jump, is not changed. Functions that call
//! each other are not changed here.

use std::collections::HashMap;

use super::{
    Binary, Bits, Block, Compare, Constant, End, Function, Instruction, Jump, Op, Place, Program,
    System, Type, Value,
};

/// The bytes of a chunk of the stack, at least.
const CHUNK: u64 = 64 * 1024;

/// The frame that is at the bottom of the first chunk: what a pop that finds it
/// returns from the function. The number of the frame that says where the
/// chunk before was is 0, and a call is 2 and more.
const EXIT: u64 = 1;

/// The program with the stack of each function that calls itself explicit.
pub fn recur(program: Program) -> Program {
    let main = program.main;
    Program {
        data: program.data,
        functions: (program.functions.into_iter().enumerate())
            .map(|(i, f)| {
                if i != main && calls_itself(i, &f) {
                    explicit(i, f)
                } else {
                    f
                }
            })
            .collect(),
        main,
    }
}

fn calls_itself(index: usize, f: &Function) -> bool {
    (f.blocks.iter().flat_map(|b| &b.instructions))
        .any(|i| matches!(&i.op, Op::Call { function, .. } if *function == index))
}

/// How a piece of the function ends.
enum Ending {
    /// As the block it was cut from did.
    Plain(End),
    /// With a call of the function itself, which goes on in the piece `next`
    /// with what the call gives as its parameters.
    Call { args: Vec<Value>, next: usize },
}

struct Piece {
    params: Vec<Value>,
    instructions: Vec<Instruction>,
    ending: Ending,
}

/// Instructions being made, with the types of the values they give.
struct Emit<'a> {
    types: &'a mut Vec<Type>,
    out: Vec<Instruction>,
}

impl Emit<'_> {
    fn fresh(&mut self, ty: Type) -> Value {
        self.types.push(ty);
        Value(self.types.len() - 1)
    }

    fn op(&mut self, op: Op, ty: Type) -> Value {
        let result = self.fresh(ty);
        self.out.push(Instruction {
            results: vec![result],
            op,
            at: Place::NONE,
        });
        result
    }

    /// The 64-bit number `n`.
    fn int(&mut self, n: u64) -> Value {
        self.op(Op::Constant(Constant::Int(n)), Type::Int(Bits::B64))
    }

    /// The address `by` bytes on from `at`, which is back when `by` is
    /// negative.
    fn moved(&mut self, at: Value, by: i64) -> Value {
        let by = self.int(by as u64);
        self.op(
            Op::Binary {
                op: Binary::Add,
                a: at,
                b: by,
            },
            Type::Address,
        )
    }

    fn store(&mut self, address: Value, value: Value) {
        let ty = self.types[value.0];
        self.out.push(Instruction {
            results: Vec::new(),
            op: Op::Store { ty, address, value },
            at: Place::NONE,
        });
    }

    fn load(&mut self, ty: Type, address: Value) -> Value {
        self.op(Op::Load { ty, address }, ty)
    }

    /// A new chunk of `bytes` bytes.
    fn chunk(&mut self, bytes: u64) -> Value {
        let count = self.int(bytes);
        self.op(
            Op::System {
                call: System::Allocate,
                args: vec![count],
            },
            Type::Address,
        )
    }
}

/// How much of a frame a value of the type takes.
fn slot(ty: Type) -> i64 {
    match ty {
        Type::Vector(_) => 16,
        _ => 8,
    }
}

/// What the function `f`, which calls itself at `index`, is with a stack of its
/// own.
fn explicit(index: usize, f: Function) -> Function {
    let Function {
        mut types,
        results,
        blocks,
        cold,
        name,
    } = f;
    // The pieces, each of the blocks cut at each call of the function.
    let mut pieces: Vec<Piece> = Vec::new();
    let mut first = Vec::with_capacity(blocks.len());
    for block in blocks {
        first.push(pieces.len());
        let (mut params, mut instructions) = (block.params, Vec::new());
        for instruction in block.instructions {
            let Instruction {
                results: gives,
                op,
                at,
            } = instruction;
            match op {
                Op::Call { function, args } if function == index => {
                    let next = pieces.len() + 1;
                    pieces.push(Piece {
                        params: std::mem::take(&mut params),
                        instructions: std::mem::take(&mut instructions),
                        ending: Ending::Call { args, next },
                    });
                    params = gives;
                }
                op => instructions.push(Instruction {
                    results: gives,
                    op,
                    at,
                }),
            }
        }
        pieces.push(Piece {
            params,
            instructions,
            ending: Ending::Plain(block.end),
        });
    }
    for piece in &mut pieces {
        if let Ending::Plain(end) = &mut piece.ending {
            for j in end.jumps_mut() {
                j.block = first[j.block];
            }
        }
    }
    let n = pieces.len();
    let values = types.len();

    // What is alive on entering each piece, and not made in it.
    let mut defined = vec![vec![false; values]; n];
    let mut read: Vec<Vec<Value>> = vec![Vec::new(); n];
    let mut next_of: Vec<Vec<usize>> = vec![Vec::new(); n];
    for (b, piece) in pieces.iter().enumerate() {
        for p in &piece.params {
            defined[b][p.0] = true;
        }
        for i in &piece.instructions {
            read[b].extend(i.op.operands());
            for r in &i.results {
                defined[b][r.0] = true;
            }
        }
        match &piece.ending {
            Ending::Plain(end) => {
                read[b].extend(end.operands());
                next_of[b] = end.successors().collect();
            }
            Ending::Call { args, next } => {
                read[b].extend(args.iter().copied());
                next_of[b] = vec![*next];
            }
        }
    }
    let mut live: Vec<Vec<bool>> = vec![vec![false; values]; n];
    loop {
        let mut changed = false;
        for b in (0..n).rev() {
            let mut now = vec![false; values];
            for v in &read[b] {
                now[v.0] = true;
            }
            for &s in &next_of[b] {
                for v in 0..values {
                    if live[s][v] && !pieces[s].params.iter().any(|p| p.0 == v) {
                        now[v] = true;
                    }
                }
            }
            for v in 0..values {
                if defined[b][v] {
                    now[v] = false;
                }
            }
            if now != live[b] {
                live[b] = now;
                changed = true;
            }
        }
        if !changed {
            break;
        }
    }
    let alive: Vec<Vec<Value>> = live
        .iter()
        .map(|l| (0..values).filter(|&v| l[v]).map(Value).collect())
        .collect();
    assert!(
        alive[first[0]].is_empty(),
        "a value is used before the start of the function makes it"
    );

    // The numbers of the blocks: the start, then a block for each piece, then the
    // pop, the two frames that are not a call, and for each call a block that
    // pushes, one that asks for a chunk, and one that pops.
    let sites: Vec<usize> = (0..n)
        .filter(|&b| matches!(pieces[b].ending, Ending::Call { .. }))
        .collect();
    let piece_at = |b: usize| 1 + b;
    let pop = 1 + n;
    let link_pop = pop + 1;
    let exit_pop = pop + 2;
    let push_at = |k: usize| pop + 3 + 3 * k;
    let grow_at = |k: usize| pop + 4 + 3 * k;
    let return_at = |k: usize| pop + 5 + 3 * k;

    let nexts: Vec<usize> = sites
        .iter()
        .map(|&b| match pieces[b].ending {
            Ending::Call { next, .. } => next,
            Ending::Plain(_) => unreachable!(),
        })
        .collect();

    // What each call keeps: the values alive when it returns, where each is in
    // the frame, and the size of the frame, which ends with the number of the call.
    struct Site {
        saved: Vec<Value>,
        at: Vec<i64>,
        size: i64,
    }
    let kept: Vec<Site> = nexts
        .iter()
        .map(|&next| {
            let saved = alive[next].clone();
            let (mut at, mut size) = (Vec::new(), 0i64);
            for v in &saved {
                at.push(size);
                size += slot(types[v.0]);
            }
            Site {
                saved,
                at,
                size: size + 8,
            }
        })
        .collect();
    let chunk = kept
        .iter()
        .map(|s| (2 * s.size as u64 + 64).next_multiple_of(4096))
        .fold(CHUNK, u64::max);

    let long = Type::Int(Bits::B64);
    let params_of_start: Vec<Type> = pieces[first[0]].params.iter().map(|p| types[p.0]).collect();
    let mut out: Vec<Block> = Vec::with_capacity(pop + 3 + 3 * sites.len());

    // The start: the first chunk, with the frame that says where to return.
    {
        let params: Vec<Value> = params_of_start
            .iter()
            .map(|t| {
                types.push(*t);
                Value(types.len() - 1)
            })
            .collect();
        let mut e = Emit {
            types: &mut types,
            out: Vec::new(),
        };
        let base = e.chunk(chunk);
        let exit = e.int(EXIT);
        e.store(base, exit);
        let sp = e.moved(base, 8);
        let left = e.int(chunk - 8);
        let mut args = params.clone();
        args.extend([sp, left]);
        out.push(Block {
            params,
            instructions: e.out,
            end: End::Jump(Jump {
                block: piece_at(first[0]),
                args,
            }),
        });
    }

    // Each piece, taking what it needs from the jump that enters it.
    let mut site_of = HashMap::new();
    for (k, &b) in sites.iter().enumerate() {
        site_of.insert(b, k);
    }
    for (b, piece) in pieces.into_iter().enumerate() {
        let sp = {
            types.push(Type::Address);
            Value(types.len() - 1)
        };
        let left = {
            types.push(long);
            Value(types.len() - 1)
        };
        let mut params = piece.params;
        let mut now: HashMap<Value, Value> = HashMap::new();
        for &v in &alive[b] {
            let ty = types[v.0];
            types.push(ty);
            let copy = Value(types.len() - 1);
            params.push(copy);
            now.insert(v, copy);
        }
        params.extend([sp, left]);
        let by = |v: Value| *now.get(&v).unwrap_or(&v);
        let mut instructions = piece.instructions;
        for i in &mut instructions {
            i.op.rename(&by);
        }
        let jump = |j: &Jump| {
            let mut args: Vec<Value> = j.args.iter().map(|&v| by(v)).collect();
            args.extend(alive[j.block].iter().map(|&v| by(v)));
            args.extend([sp, left]);
            Jump {
                block: piece_at(j.block),
                args,
            }
        };
        let end = match piece.ending {
            Ending::Plain(End::Return(vals)) => {
                let mut args: Vec<Value> = vals.iter().map(|&v| by(v)).collect();
                args.extend([sp, left]);
                End::Jump(Jump { block: pop, args })
            }
            Ending::Plain(End::Jump(j)) => End::Jump(jump(&j)),
            Ending::Plain(End::Switch { on, jumps }) => End::Switch {
                on: by(on),
                jumps: jumps.iter().map(jump).collect(),
            },
            Ending::Call { args, .. } => {
                let k = site_of[&b];
                let site = &kept[k];
                let mut vals: Vec<Value> = site.saved.iter().map(|&v| by(v)).collect();
                vals.extend(args.iter().map(|&v| by(v)));
                vals.extend([sp, left]);
                let mut e = Emit {
                    types: &mut types,
                    out: instructions,
                };
                let size = e.int(site.size as u64);
                let short = e.op(
                    Op::Compare {
                        op: Compare::LessU,
                        a: left,
                        b: size,
                    },
                    Type::Int(Bits::B8),
                );
                instructions = e.out;
                End::Switch {
                    on: short,
                    jumps: vec![
                        Jump {
                            block: push_at(k),
                            args: vals.clone(),
                        },
                        Jump {
                            block: grow_at(k),
                            args: vals,
                        },
                    ],
                }
            }
        };
        out.push(Block {
            params,
            instructions,
            end,
        });
    }

    // The pop: the number of the frame on top says where to go on.
    let result_types = results.clone();
    let fresh_results = |types: &mut Vec<Type>| -> Vec<Value> {
        result_types
            .iter()
            .map(|t| {
                types.push(*t);
                Value(types.len() - 1)
            })
            .collect()
    };
    let state = |types: &mut Vec<Type>| -> (Value, Value) {
        types.push(Type::Address);
        let sp = Value(types.len() - 1);
        types.push(long);
        (sp, Value(types.len() - 1))
    };
    {
        let mut params = fresh_results(&mut types);
        let (sp, left) = state(&mut types);
        let vals = params.clone();
        params.extend([sp, left]);
        let mut e = Emit {
            types: &mut types,
            out: Vec::new(),
        };
        let at = e.moved(sp, -8);
        let id = e.load(long, at);
        let mut args = vals;
        args.extend([sp, left]);
        let to = |block: usize| Jump {
            block,
            args: args.clone(),
        };
        let mut jumps = vec![to(link_pop), to(exit_pop)];
        jumps.extend((0..sites.len()).map(|k| to(return_at(k))));
        out.push(Block {
            params,
            instructions: e.out,
            end: End::Switch { on: id, jumps },
        });
    }
    // A chunk that is empty: go back to the one before it, and pop again.
    {
        let mut params = fresh_results(&mut types);
        let (sp, left) = state(&mut types);
        let vals = params.clone();
        params.extend([sp, left]);
        let mut e = Emit {
            types: &mut types,
            out: Vec::new(),
        };
        let at = e.moved(sp, -24);
        let before = e.load(Type::Address, at);
        let at = e.moved(sp, -16);
        let room = e.load(long, at);
        let mut args = vals;
        args.extend([before, room]);
        out.push(Block {
            params,
            instructions: e.out,
            end: End::Jump(Jump { block: pop, args }),
        });
    }
    // The frame at the bottom: the function returns.
    {
        let mut params = fresh_results(&mut types);
        let vals = params.clone();
        let (sp, left) = state(&mut types);
        params.extend([sp, left]);
        out.push(Block {
            params,
            instructions: Vec::new(),
            end: End::Return(vals),
        });
    }
    // For each call: push, a new chunk, and pop.
    for (k, site) in kept.iter().enumerate() {
        let next = nexts[k];
        let saved_types: Vec<Type> = site.saved.iter().map(|v| types[v.0]).collect();
        let arg_types = params_of_start.clone();
        let block_params = |types: &mut Vec<Type>| -> (Vec<Value>, Vec<Value>, Value, Value) {
            let saved: Vec<Value> = saved_types
                .iter()
                .map(|t| {
                    types.push(*t);
                    Value(types.len() - 1)
                })
                .collect();
            let args: Vec<Value> = arg_types
                .iter()
                .map(|t| {
                    types.push(*t);
                    Value(types.len() - 1)
                })
                .collect();
            let (sp, left) = state(types);
            (saved, args, sp, left)
        };
        // Push.
        {
            let (saved, args, sp, left) = block_params(&mut types);
            let mut params = saved.clone();
            params.extend(args.iter().copied());
            params.extend([sp, left]);
            let mut e = Emit {
                types: &mut types,
                out: Vec::new(),
            };
            for (v, at) in saved.iter().zip(&site.at) {
                let place = e.moved(sp, *at);
                e.store(place, *v);
            }
            let id = e.int(2 + k as u64);
            let place = e.moved(sp, site.size - 8);
            e.store(place, id);
            let sp2 = e.moved(sp, site.size);
            let size = e.int(site.size as u64);
            let left2 = e.op(
                Op::Binary {
                    op: Binary::Sub,
                    a: left,
                    b: size,
                },
                long,
            );
            let mut go = args;
            go.extend([sp2, left2]);
            out.push(Block {
                params,
                instructions: e.out,
                end: End::Jump(Jump {
                    block: piece_at(first[0]),
                    args: go,
                }),
            });
        }
        // A new chunk: its first frame says where the chunk before ended.
        {
            let (saved, args, sp, left) = block_params(&mut types);
            let mut params = saved.clone();
            params.extend(args.iter().copied());
            params.extend([sp, left]);
            let mut e = Emit {
                types: &mut types,
                out: Vec::new(),
            };
            let base = e.chunk(chunk);
            e.store(base, sp);
            let place = e.moved(base, 8);
            e.store(place, left);
            let zero = e.int(0);
            let place = e.moved(base, 16);
            e.store(place, zero);
            let sp2 = e.moved(base, 24);
            let left2 = e.int(chunk - 24);
            let mut go = saved;
            go.extend(args);
            go.extend([sp2, left2]);
            out.push(Block {
                params,
                instructions: e.out,
                end: End::Jump(Jump {
                    block: push_at(k),
                    args: go,
                }),
            });
        }
        // Pop: the values come back, and the piece that goes on is entered.
        {
            let mut params = fresh_results(&mut types);
            let (sp, left) = state(&mut types);
            let vals = params.clone();
            params.extend([sp, left]);
            let mut e = Emit {
                types: &mut types,
                out: Vec::new(),
            };
            let base = e.moved(sp, -site.size);
            let mut go = vals;
            for (ty, at) in saved_types.iter().zip(&site.at) {
                let place = e.moved(base, *at);
                go.push(e.load(*ty, place));
            }
            let size = e.int(site.size as u64);
            let left2 = e.op(
                Op::Binary {
                    op: Binary::Add,
                    a: left,
                    b: size,
                },
                long,
            );
            go.extend([base, left2]);
            out.push(Block {
                params,
                instructions: e.out,
                end: End::Jump(Jump {
                    block: piece_at(next),
                    args: go,
                }),
            });
        }
    }
    Function {
        types,
        results,
        blocks: out,
        cold,
        name,
    }
}
