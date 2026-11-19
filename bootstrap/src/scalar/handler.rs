//! The state of the `alloc` handler, passed and never global: two stacks of
//! memory, each held as where its free memory begins and how many bytes of it
//! are left. One is where a function's result is built, and one is where its
//! temporaries are.
//!
//! The lowering asks the system for memory wherever the source builds a list
//! or a text, with `System::Allocate`, as if each were a request of its own.
//! `install` turns that into taking room from a stack. A function takes the
//! pair for its results and the pair for its temporaries as its first
//! parameters, each only when it uses it, and gives back the pair for its
//! results. An address it asks for comes from the pair for results when the
//! function may return memory that holds it, and from the pair for temporaries
//! when it cannot, as `escape` works out. A call whose result the function
//! returns is given the pairs as they are. A call whose result it only uses is
//! given them swapped, so that the callee's result is built where the caller
//! keeps its temporaries, and what the callee does with its own temporaries
//! lies above everything the caller has in the stack it returns. A function
//! goes on with the pair for temporaries it had before a call, which releases
//! what the call used. Each block takes the pairs from the jump that enters
//! it. The program's entry starts both stacks empty, so the system is not
//! asked before the program first needs room. A program that asks for nothing
//! is left as it is. Nothing is given back to the system.

use super::{
    Binary, Bits, Builder, Compare, Constant, End, Function, Instruction, Op, Program, System,
    Type, Value, escape, runtime,
};

/// How many bytes the system gives at a time, at least.
pub const PIECE: u64 = 64 * 1024;

/// What a piece spends at its end: the number of bytes it holds for the
/// program, and the address of the end of the piece that follows it, or
/// nothing.
const TRAILER: u64 = 16;

/// The bit of a request's count that asks only for what the piece has room for.
pub const TRY: u64 = 1 << 63;

const NEXT: Type = Type::Address;
const LEFT: Type = Type::Int(Bits::B64);
/// The values of one stack, in the order they are passed.
const PAIR: [Type; 2] = [NEXT, LEFT];

/// Whether the function of the program is the handler's `take`, or calls it,
/// which is what the functions that build lists and texts do.
pub fn takes_room(program: &Program, f: &Function) -> bool {
    let take = take();
    *f == take
        || (f.blocks.iter().flat_map(|b| &b.instructions)).any(
            |i| matches!(&i.op, Op::Call { function, .. } if program.functions[*function] == take),
        )
}

/// Which stacks a function takes: the one for results, and the one for
/// temporaries.
#[derive(Clone, Copy, Default, PartialEq)]
struct Uses {
    results: bool,
    temporaries: bool,
}

impl Uses {
    fn any(self) -> bool {
        self.results || self.temporaries
    }
}

/// The two stacks of a function, as they are at a place in it, for the ones it
/// takes.
#[derive(Clone, Copy)]
struct State {
    results: Option<[Value; 2]>,
    temporaries: Option<[Value; 2]>,
}

impl State {
    /// The values in the order they are passed to a function and to a block.
    fn values(&self) -> Vec<Value> {
        [self.results, self.temporaries]
            .into_iter()
            .flatten()
            .flatten()
            .collect()
    }
}

/// The program with the stacks passed to every function that needs room.
pub fn install(program: Program) -> Program {
    let given: Vec<Vec<bool>> = program.functions.iter().map(escape::given_back).collect();
    let uses = needing(&program, &given);
    if !uses[program.main].any() {
        return program;
    }
    let at_take = program.functions.len();
    let main = program.functions[program.main].clone();
    let mut functions: Vec<Function> = (program.functions.into_iter().enumerate())
        .map(|(k, f)| {
            if uses[k].any() {
                with_state(f, &given[k], &uses, k == program.main, at_take)
            } else {
                f
            }
        })
        .collect();
    functions.push(take());
    functions.push(ready(at_take));
    let mut data = program.data;
    // What a stack that has no piece reads at its end: a piece that holds
    // nothing and has none after it.
    data.push(vec![0; TRAILER as usize]);
    functions.push(entry(
        &main,
        program.main,
        uses[program.main],
        data.len() - 1,
    ));
    Program {
        data,
        main: at_take + 2,
        functions,
    }
}

/// Whether what a call gives is memory that the function returns, so that the
/// callee builds it in the stack for results.
fn gives_back(f: &Function, given: &[bool], results: &[Value]) -> bool {
    results
        .iter()
        .any(|v| f.types[v.0] == Type::Address && given[v.0])
}

/// Which stacks each function takes, which is what it needs where it asks the
/// system for memory, and where it calls a function that needs some.
fn needing(program: &Program, given: &[Vec<bool>]) -> Vec<Uses> {
    let mut uses = vec![Uses::default(); program.functions.len()];
    loop {
        let mut changed = false;
        for (k, f) in program.functions.iter().enumerate() {
            let now = needing_of(f, &given[k], &uses);
            if now != uses[k] {
                uses[k] = now;
                changed = true;
            }
        }
        if !changed {
            return uses;
        }
    }
}

/// A new pair of values of a stack in the function.
fn fresh(f: &mut Function) -> [Value; 2] {
    PAIR.map(|ty| {
        f.types.push(ty);
        Value(f.types.len() - 1)
    })
}

/// The function with the stacks it takes before its parameters, and the one
/// for results before its results, taking room where it asked the system, and
/// giving the stacks to each call that needs them, and to each block.
fn with_state(
    mut f: Function,
    given: &[bool],
    uses: &[Uses],
    first: bool,
    take: usize,
) -> Function {
    let me = needing_of(&f, given, uses);
    let start = |f: &mut Function| State {
        results: me.results.then(|| fresh(f)),
        temporaries: me.temporaries.then(|| fresh(f)),
    };
    // What the stacks are at the start of each block.
    let at_start: Vec<State> = (0..f.blocks.len()).map(|_| start(&mut f)).collect();
    for (block, start) in f.blocks.iter_mut().zip(&at_start) {
        block.params.splice(0..0, start.values());
    }
    for (k, start) in at_start.into_iter().enumerate() {
        let mut now = start;
        let instructions = std::mem::take(&mut f.blocks[k].instructions);
        let mut kept: Vec<Instruction> = Vec::with_capacity(instructions.len());
        for mut i in instructions {
            match &mut i.op {
                Op::System {
                    call: System::Allocate,
                    args,
                } => {
                    let next = fresh(&mut f);
                    let stack = if given[i.results[0].0] {
                        &mut now.results
                    } else {
                        &mut now.temporaries
                    };
                    let [at, left] = stack.expect("the function takes the stack it asks");
                    i.op = Op::Call {
                        function: take,
                        args: vec![at, left, args[0]],
                    };
                    i.results.splice(0..0, next);
                    *stack = Some(next);
                }
                Op::Call { function, args } if uses[*function].any() => {
                    let callee = uses[*function];
                    let returned = gives_back(&f, given, &i.results);
                    // What the callee does with a stack it is given for its
                    // temporaries is not given back, so a stack that has no
                    // piece yet would lose the one it takes, and the callee may
                    // lend what it is given to a function that does so. The
                    // function the program starts with makes each stack it
                    // passes have a piece first.
                    if first && callee.temporaries {
                        let (own, other) = if returned {
                            (&mut now.results, &mut now.temporaries)
                        } else {
                            (&mut now.temporaries, &mut now.results)
                        };
                        let passed = [(own, callee.results), (other, true)];
                        for (stack, takes) in passed {
                            if !takes {
                                continue;
                            }
                            let [at, left] = stack.expect("the function takes what it gives");
                            let made = fresh(&mut f);
                            kept.push(Instruction {
                                results: made.to_vec(),
                                op: Op::Call {
                                    function: take + 1,
                                    args: vec![at, left],
                                },
                            });
                            *stack = Some(made);
                        }
                    }
                    // What the callee takes for its results and for its
                    // temporaries, from what the function has.
                    let (for_results, for_temporaries) = if returned {
                        (now.results, now.temporaries)
                    } else {
                        (now.temporaries, now.results)
                    };
                    let mut passed = Vec::new();
                    if callee.results {
                        passed.extend(for_results.expect("the function takes what it gives"));
                    }
                    if callee.temporaries {
                        passed.extend(for_temporaries.expect("the function takes what it gives"));
                    }
                    args.splice(0..0, passed);
                    if callee.results {
                        // What the callee gives back is the stack it built
                        // its result in, moved on by it. The other goes on as
                        // it was before the call.
                        let next = fresh(&mut f);
                        i.results.splice(0..0, next);
                        if returned {
                            now.results = Some(next);
                        } else {
                            now.temporaries = Some(next);
                        }
                    }
                }
                _ => {}
            }
            kept.push(i);
        }
        f.blocks[k].instructions = kept;
        match &mut f.blocks[k].end {
            End::Jump(jump) => {
                jump.args.splice(0..0, now.values());
            }
            End::Switch { jumps, .. } => {
                for jump in jumps {
                    jump.args.splice(0..0, now.values());
                }
            }
            End::Return(values) => {
                values.splice(0..0, now.results.into_iter().flatten());
            }
        }
    }
    if me.results {
        f.results.splice(0..0, PAIR);
    }
    f
}

/// Which stacks the function takes.
fn needing_of(f: &Function, given: &[bool], uses: &[Uses]) -> Uses {
    let mut now = Uses::default();
    for i in f.blocks.iter().flat_map(|b| &b.instructions) {
        match &i.op {
            Op::System {
                call: System::Allocate,
                ..
            } => {
                if given[i.results[0].0] {
                    now.results = true;
                } else {
                    now.temporaries = true;
                }
            }
            Op::Call { function, .. } if uses[*function].any() => {
                let callee = uses[*function];
                if gives_back(f, given, &i.results) {
                    now.results |= callee.results;
                    now.temporaries |= callee.temporaries;
                } else {
                    now.temporaries |= callee.results;
                    now.results |= callee.temporaries;
                }
            }
            _ => {}
        }
    }
    now
}

/// Takes the pair of a stack and a number of bytes, and gives the pair after
/// it and the address of that many bytes. The count is made a multiple of 8,
/// so that every address is. When the piece has the room, it is a compare, an
/// add, and a subtraction. When it has not, the stack goes to the piece that
/// follows when there is one that is big enough, and otherwise the system
/// gives a piece of 64 KB, or of what the count and the end of a piece need
/// rounded up to a multiple of 64 KB when that is more, and it is put between
/// the two. What was left of the old piece is not used again until the stack
/// is back at its start. A piece ends with the number of bytes it holds and the
/// address of the end of the piece after it, or nothing, so that a piece is
/// found again from where its stack ends. A count too large to round is asked
/// of the system as it is, and refused.
pub fn take() -> Function {
    let mut f = Builder::new();
    let p = f.params(&[NEXT, LEFT, LEFT]);
    let (next, left, count) = (p[0], p[1], p[2]);
    let seven = runtime::int(&mut f, 7);
    let padded = f.binary(Binary::Add, count, seven);
    let mask = runtime::int(&mut f, !7);
    let size = f.binary(Binary::And, padded, mask);
    let huge_from = runtime::int(&mut f, 1 << 62);
    let huge = f.compare(Compare::LessU, huge_from, count);
    let short = f.compare(Compare::LessU, left, size);
    let no_room = f.binary(Binary::Or, short, huge);
    let (slow, _) = f.block(&[]);
    let (fast, _) = f.block(&[]);
    let (look, _) = f.block(&[]);
    let (have, _) = f.block(&[]);
    let (reuse, _) = f.block(&[]);
    // What the system is asked for, the end of the piece the stack was in, the
    // piece that follows it, and whether that end is a piece's to write in.
    let (ask, a) = f.block(&[LEFT, NEXT, NEXT, Type::Int(Bits::B8)]);
    let (done, _) = f.block(&[]);
    let (fix, _) = f.block(&[]);
    let (trying, _) = f.block(&[]);
    let (ask_raw, _) = f.block(&[]);
    let (attempt, _) = f.block(&[]);
    let (grant, _) = f.block(&[]);
    let (refuse, _) = f.block(&[]);
    f.switch(no_room, vec![(fast, vec![]), (slow, vec![])]);
    f.at(fast);
    let after = f.binary(Binary::Add, next, size);
    let rest = f.binary(Binary::Sub, left, size);
    f.ret(vec![after, rest, next]);
    // The end of the piece is where it keeps the bytes it holds and where the
    // piece after it ends. A stack that has no piece is at one that holds 0.
    f.at(slow);
    let end = f.binary(Binary::Add, next, left);
    let eight = runtime::int(&mut f, 8);
    let link = f.binary(Binary::Add, end, eight);
    let holds = f.load(LEFT, end);
    let zero = runtime::int(&mut f, 0);
    let bare = f.compare(Compare::Equal, holds, zero);
    let one = f.int(Bits::B8, 1);
    let patch = f.binary(Binary::Xor, bare, one);
    let linked = f.load(LEFT, link);
    let nothing = f.compare(Compare::Equal, linked, zero);
    let after_at = f.load(NEXT, link);
    // A request that no piece of the usual size has room for is given a piece
    // with room for it to double, which a list that grows does, in place.
    let fits_from = runtime::int(&mut f, PIECE - TRAILER);
    let large = f.compare(Compare::LessU, fits_from, size);
    let twice = f.binary(Binary::Add, size, size);
    let room = f.choose(large, twice, size);
    let more = runtime::int(&mut f, TRAILER + PIECE - 1);
    let up = f.binary(Binary::Add, room, more);
    let pieces = runtime::int(&mut f, !(PIECE - 1));
    let wanted = f.binary(Binary::And, up, pieces);
    let none = f.null();
    f.switch(huge, vec![(look, vec![]), (trying, vec![])]);
    // A count with its top bit set asks for the rest of it only if the piece
    // has room, and is given nothing when it has not, so that a list can ask
    // for the room it lacks after it without taking a piece it will not use.
    f.at(trying);
    let flag = runtime::int(&mut f, TRY);
    let one_word = runtime::int(&mut f, 1);
    let below = f.binary(Binary::Sub, flag, one_word);
    let tried = f.compare(Compare::LessU, below, count);
    f.switch(tried, vec![(ask_raw, vec![]), (attempt, vec![])]);
    f.at(ask_raw);
    f.jump(ask, vec![count, end, none, patch]);
    f.at(attempt);
    let wanted_bytes = f.binary(Binary::And, count, below);
    let wanted_up = f.binary(Binary::Add, wanted_bytes, seven);
    let wanted_size = f.binary(Binary::And, wanted_up, mask);
    let lacks = f.compare(Compare::LessU, left, wanted_size);
    f.switch(lacks, vec![(grant, vec![]), (refuse, vec![])]);
    f.at(grant);
    let after = f.binary(Binary::Add, next, wanted_size);
    let rest = f.binary(Binary::Sub, left, wanted_size);
    f.ret(vec![after, rest, next]);
    f.at(refuse);
    f.ret(vec![next, left, none]);
    f.at(look);
    f.switch(
        nothing,
        vec![(have, vec![]), (ask, vec![wanted, end, none, patch])],
    );
    f.at(have);
    let holds_after = f.load(LEFT, after_at);
    let small = f.compare(Compare::LessU, holds_after, size);
    f.switch(
        small,
        vec![(reuse, vec![]), (ask, vec![wanted, end, after_at, patch])],
    );
    f.at(reuse);
    let back = f.binary(Binary::Sub, zero, holds_after);
    let start = f.binary(Binary::Add, after_at, back);
    let after = f.binary(Binary::Add, start, size);
    let rest = f.binary(Binary::Sub, holds_after, size);
    f.ret(vec![after, rest, start]);
    f.at(ask);
    let (asked, old_end, old_link, patch) = (a[0], a[1], a[2], a[3]);
    let start = f.allocate(asked);
    let trailer = runtime::int(&mut f, TRAILER);
    let holds_now = f.binary(Binary::Sub, asked, trailer);
    let new_end = f.binary(Binary::Add, start, holds_now);
    f.store(new_end, holds_now);
    let new_link = f.binary(Binary::Add, new_end, eight);
    f.store(new_link, old_link);
    let after = f.binary(Binary::Add, start, size);
    let rest = f.binary(Binary::Sub, holds_now, size);
    f.switch(patch, vec![(done, vec![]), (fix, vec![])]);
    f.at(done);
    f.ret(vec![after, rest, start]);
    f.at(fix);
    let old_link_at = f.binary(Binary::Add, old_end, eight);
    f.store(old_link_at, new_end);
    f.ret(vec![after, rest, start]);
    f.finish(&[NEXT, LEFT, Type::Address])
}

/// Takes the pair of a stack, and gives it as it is when it has a piece, and
/// otherwise a piece the system gives, which holds 8 bytes less than it did.
/// It is what makes a stack that a callee may use for its temporaries one that
/// remembers the piece, since the callee does not give the stack back.
pub fn ready(take: usize) -> Function {
    let mut f = Builder::new();
    let p = f.params(&[NEXT, LEFT]);
    let (next, left) = (p[0], p[1]);
    let (has, _) = f.block(&[]);
    let (bare, _) = f.block(&[]);
    let end = f.binary(Binary::Add, next, left);
    let holds = f.load(LEFT, end);
    let zero = runtime::int(&mut f, 0);
    let none = f.compare(Compare::Equal, holds, zero);
    f.switch(none, vec![(has, vec![]), (bare, vec![])]);
    f.at(has);
    f.ret(vec![next, left]);
    f.at(bare);
    let one = runtime::int(&mut f, 1);
    let made = f.call(take, vec![next, left, one], &[NEXT, LEFT, NEXT]);
    f.ret(vec![made[0], made[1]]);
    f.finish(&PAIR)
}

/// What starts the program: it calls `main`, which took what the system gives,
/// with stacks that have no bytes before them.
fn entry(main: &Function, at: usize, uses: Uses, empty: usize) -> Function {
    let mut f = Builder::new();
    let types: Vec<Type> = (main.blocks[0].params.iter())
        .map(|v| main.types[v.0])
        .collect();
    let params = f.params(&types);
    let mut args = Vec::new();
    for _ in 0..usize::from(uses.results) + usize::from(uses.temporaries) {
        args.push(f.emit(Op::Constant(Constant::Data(empty)), &[NEXT])[0]);
        args.push(runtime::int(&mut f, 0));
    }
    args.extend(params);
    let mut results = Vec::new();
    if uses.results {
        results.extend(PAIR);
    }
    results.extend(&main.results);
    f.call(at, args, &results);
    f.ret(Vec::new());
    f.finish(&[])
}
