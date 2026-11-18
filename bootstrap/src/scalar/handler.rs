//! The state of the `alloc` handler, passed and never global: where the free
//! memory of the program's current piece begins, and how many bytes of it are
//! left.
//!
//! The lowering asks the system for memory wherever the source builds a list
//! or a text, with `System::Allocate`, as if each were a request of its own.
//! `install` turns that into taking room from a piece of 64 KB that the system
//! gave. Every function that asks, or calls a function that does, takes the
//! two values of the state as its first parameters, passes them on to each
//! call, and gives them back as its first results, and each block it has takes
//! them from the block that jumps to it. So a task or an arena of its own is a
//! pair of values of its own. The program's entry starts with a piece that has
//! no bytes, so the system is not asked before the program first needs room.
//! A program that asks for nothing is left as it is. Nothing is given back.

use super::{
    Binary, Bits, Builder, Compare, Constant, End, Function, Instruction, Op, Program, System,
    Type, Value, runtime,
};

/// How many bytes the system gives at a time, at least.
pub const PIECE: u64 = 64 * 1024;

const NEXT: Type = Type::Address;
const LEFT: Type = Type::Int(Bits::B64);
/// The values of the state, in the order they are passed.
const STATE: [Type; 2] = [NEXT, LEFT];

/// Whether the function of the program is the handler's `take`, or calls it,
/// which is what the functions that build lists and texts do.
pub fn takes_room(program: &Program, f: &Function) -> bool {
    let take = take();
    *f == take
        || (f.blocks.iter().flat_map(|b| &b.instructions)).any(
            |i| matches!(&i.op, Op::Call { function, .. } if program.functions[*function] == take),
        )
}

/// The program with the state passed to every function that needs room.
pub fn install(program: &Program) -> Program {
    let needs = needing(program);
    if !needs[program.main] {
        return program.clone();
    }
    let at_take = program.functions.len();
    let mut functions: Vec<Function> = (program.functions.iter().enumerate())
        .map(|(k, f)| {
            if needs[k] {
                with_state(f, &needs, at_take)
            } else {
                f.clone()
            }
        })
        .collect();
    functions.push(take());
    functions.push(entry(&program.functions[program.main], program.main));
    Program {
        data: program.data.clone(),
        main: at_take + 1,
        functions,
    }
}

/// Which functions ask the system for memory, or call one that does.
fn needing(program: &Program) -> Vec<bool> {
    let mut needs = vec![false; program.functions.len()];
    loop {
        let mut changed = false;
        for (k, f) in program.functions.iter().enumerate() {
            if needs[k] {
                continue;
            }
            let asks = (f.blocks.iter().flat_map(|b| &b.instructions)).any(|i| match &i.op {
                Op::System {
                    call: System::Allocate,
                    ..
                } => true,
                Op::Call { function, .. } => needs[*function],
                _ => false,
            });
            if asks {
                needs[k] = true;
                changed = true;
            }
        }
        if !changed {
            return needs;
        }
    }
}

/// A new pair of values of the state in the function.
fn fresh(f: &mut Function) -> [Value; 2] {
    STATE.map(|ty| {
        f.types.push(ty);
        Value(f.types.len() - 1)
    })
}

/// The function with the state before its parameters and its results,
/// taking room where it asked the system, and giving the state it has to each
/// call that needs it, and to each block.
fn with_state(f: &Function, needs: &[bool], take: usize) -> Function {
    let mut f = f.clone();
    // What the state is at the start of each block.
    let at_start: Vec<[Value; 2]> = (0..f.blocks.len()).map(|_| fresh(&mut f)).collect();
    for (block, start) in f.blocks.iter_mut().zip(&at_start) {
        block.params.splice(0..0, *start);
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
                    i.op = Op::Call {
                        function: take,
                        args: vec![now[0], now[1], args[0]],
                    };
                    i.results.splice(0..0, next);
                    now = next;
                }
                Op::Call { function, args } if needs[*function] => {
                    let next = fresh(&mut f);
                    args.splice(0..0, now);
                    i.results.splice(0..0, next);
                    now = next;
                }
                _ => {}
            }
            kept.push(i);
        }
        f.blocks[k].instructions = kept;
        match &mut f.blocks[k].end {
            End::Jump(jump) => {
                jump.args.splice(0..0, now);
            }
            End::Switch { jumps, .. } => {
                for jump in jumps {
                    jump.args.splice(0..0, now);
                }
            }
            End::Return(values) => {
                values.splice(0..0, now);
            }
        }
    }
    f.results.splice(0..0, STATE);
    f
}

/// Takes the state and a number of bytes, and gives the state after it and
/// the address of that many bytes. The count is made a multiple of 8, so that
/// every address is. When the piece has the room, it is a compare, an add, and
/// a subtraction. When it has not, the system gives a piece of 64 KB, or of
/// the count rounded up to a multiple of 64 KB when that is more, and what was
/// left of the old is not used again. A count too large to round is asked of
/// the system as it is, and refused.
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
    f.switch(no_room, vec![(fast, vec![]), (slow, vec![])]);
    f.at(fast);
    let after = f.binary(Binary::Add, next, size);
    let rest = f.binary(Binary::Sub, left, size);
    f.ret(vec![after, rest, next]);
    f.at(slow);
    let piece = runtime::int(&mut f, PIECE);
    let more = f.compare(Compare::LessU, piece, size);
    let up = runtime::int(&mut f, PIECE - 1);
    let rounded = f.binary(Binary::Add, size, up);
    let pieces = runtime::int(&mut f, !(PIECE - 1));
    let own = f.binary(Binary::And, rounded, pieces);
    let wanted = f.choose(more, own, piece);
    let asked = f.choose(huge, count, wanted);
    let start = f.allocate(asked);
    let after = f.binary(Binary::Add, start, size);
    let rest = f.binary(Binary::Sub, asked, size);
    f.ret(vec![after, rest, start]);
    f.finish(&[NEXT, LEFT, Type::Address])
}

/// What starts the program: it calls `main`, which took what the system gives,
/// with a state of a piece that has no bytes before them.
fn entry(main: &Function, at: usize) -> Function {
    let mut f = Builder::new();
    let types: Vec<Type> = (main.blocks[0].params.iter())
        .map(|v| main.types[v.0])
        .collect();
    let params = f.params(&types);
    let next = f.emit(Op::Constant(Constant::Null), &[NEXT])[0];
    let left = runtime::int(&mut f, 0);
    let mut args = vec![next, left];
    args.extend(params);
    let mut results = STATE.to_vec();
    results.extend(&main.results);
    f.call(at, args, &results);
    f.ret(Vec::new());
    f.finish(&[])
}
