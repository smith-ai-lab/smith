//! Says whether a program in the scalar form is well formed: every value is
//! defined once, is used only where its definition reaches, and is used at
//! its type, and every jump gives a block the parameters it takes.

use super::{Binary, Bits, Constant, End, Function, Jump, Op, Program, System, Type, Value};

/// Why a function is not well formed.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Why {
    /// A value, a block, a function, or a text that does not exist.
    Missing,
    /// A value that is defined more than once.
    DefinedTwice(Value),
    /// A value used where its definition does not reach: in a block it
    /// does not dominate, or before it in its own block.
    NotReached(Value),
    /// A value used, or defined, at a type other than its own.
    WrongType(Value),
    /// A jump, a call, or a return that gives other than the number of
    /// values that its place takes.
    WrongCount,
}

/// A function that is not well formed, and why.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Malformed {
    pub function: usize,
    pub why: Why,
}

/// Checks every function of the program.
pub fn check(program: &Program) -> Result<(), Malformed> {
    let main = (program.functions.get(program.main)).ok_or(Malformed {
        function: program.main,
        why: Why::Missing,
    })?;
    if !main.results.is_empty() || main.blocks.first().is_none_or(|b| !b.params.is_empty()) {
        return Err(Malformed {
            function: program.main,
            why: Why::WrongCount,
        });
    }
    for (function, f) in program.functions.iter().enumerate() {
        function_of(program, f).map_err(|why| Malformed { function, why })?;
    }
    Ok(())
}

/// Where a value is defined: its block, and its place there, which is 0 for
/// a parameter and one more than its instruction's place for a result.
type Definition = (usize, usize);

fn function_of(program: &Program, f: &Function) -> Result<(), Why> {
    if f.blocks.is_empty() {
        return Err(Why::Missing);
    }
    let mut defined: Vec<Option<Definition>> = vec![None; f.types.len()];
    for (b, block) in f.blocks.iter().enumerate() {
        let params = block.params.iter().map(|v| (v, 0));
        let results = (block.instructions.iter().enumerate())
            .flat_map(|(i, instruction)| instruction.results.iter().map(move |v| (v, i + 1)));
        for (v, at) in params.chain(results) {
            let slot = defined.get_mut(v.0).ok_or(Why::Missing)?;
            if slot.is_some() {
                return Err(Why::DefinedTwice(*v));
            }
            *slot = Some((b, at));
        }
    }
    let reaching = reaching(f, &defined)?;
    for (b, block) in f.blocks.iter().enumerate() {
        let used = |v: &Value, at: usize, want: Type| -> Result<(), Why> {
            let (block_of, place) = (defined.get(v.0).copied().flatten()).ok_or(Why::Missing)?;
            let reached = if block_of == b {
                place <= at
            } else {
                reaching[b][v.0]
            };
            if !reached {
                return Err(Why::NotReached(*v));
            }
            if f.types[v.0] != want {
                return Err(Why::WrongType(*v));
            }
            Ok(())
        };
        for (i, instruction) in block.instructions.iter().enumerate() {
            match &instruction.op {
                Op::Constant(c) => {
                    let [result] = instruction.results[..] else {
                        return Err(Why::WrongCount);
                    };
                    let fits = match c {
                        Constant::Int(_) => matches!(f.types[result.0], Type::Int(_)),
                        Constant::Null => f.types[result.0] == Type::Address,
                        Constant::Data(t) => {
                            if *t >= program.data.len() {
                                return Err(Why::Missing);
                            }
                            f.types[result.0] == Type::Address
                        }
                    };
                    if !fits {
                        return Err(Why::WrongType(result));
                    }
                }
                Op::Binary { a, b, .. } => {
                    let [result] = instruction.results[..] else {
                        return Err(Why::WrongCount);
                    };
                    let ty = f.types[result.0];
                    match ty {
                        Type::Int(_) => {
                            used(a, i, ty)?;
                            used(b, i, ty)?;
                        }
                        // The only arithmetic on an address moves it by a
                        // number of bytes.
                        Type::Address => {
                            if !matches!(
                                instruction.op,
                                Op::Binary {
                                    op: Binary::Add,
                                    ..
                                }
                            ) {
                                return Err(Why::WrongType(result));
                            }
                            used(a, i, Type::Address)?;
                            used(b, i, Type::Int(Bits::B64))?;
                        }
                    }
                }
                Op::Compare { a, b, .. } => {
                    let [result] = instruction.results[..] else {
                        return Err(Why::WrongCount);
                    };
                    if f.types[result.0] != Type::Int(Bits::B8) {
                        return Err(Why::WrongType(result));
                    }
                    let ty = f.types.get(a.0).copied().ok_or(Why::Missing)?;
                    if !matches!(ty, Type::Int(_)) {
                        return Err(Why::WrongType(*a));
                    }
                    used(a, i, ty)?;
                    used(b, i, ty)?;
                }
                Op::Widen { value, .. } | Op::Narrow(value) => {
                    let [result] = instruction.results[..] else {
                        return Err(Why::WrongCount);
                    };
                    let (Type::Int(to), Some(&Type::Int(from))) =
                        (f.types[result.0], f.types.get(value.0))
                    else {
                        return Err(Why::WrongType(result));
                    };
                    let widens = matches!(instruction.op, Op::Widen { .. });
                    if (widens && from.count() >= to.count())
                        || (!widens && from.count() <= to.count())
                    {
                        return Err(Why::WrongType(result));
                    }
                    used(value, i, Type::Int(from))?;
                }
                Op::Choose {
                    on,
                    then,
                    otherwise,
                } => {
                    let [result] = instruction.results[..] else {
                        return Err(Why::WrongCount);
                    };
                    let ty = f.types[result.0];
                    used(on, i, Type::Int(Bits::B8))?;
                    used(then, i, ty)?;
                    used(otherwise, i, ty)?;
                }
                Op::Call { function, args } => {
                    let callee = program.functions.get(*function).ok_or(Why::Missing)?;
                    let params = callee.blocks.first().ok_or(Why::Missing)?.params.iter();
                    let wanted: Vec<Type> = params.map(|v| callee.types[v.0]).collect();
                    if args.len() != wanted.len()
                        || instruction.results.len() != callee.results.len()
                    {
                        return Err(Why::WrongCount);
                    }
                    for (a, want) in args.iter().zip(wanted) {
                        used(a, i, want)?;
                    }
                    for (r, want) in instruction.results.iter().zip(&callee.results) {
                        if f.types[r.0] != *want {
                            return Err(Why::WrongType(*r));
                        }
                    }
                }
                Op::Load { ty, address } => {
                    let [result] = instruction.results[..] else {
                        return Err(Why::WrongCount);
                    };
                    if f.types[result.0] != *ty {
                        return Err(Why::WrongType(result));
                    }
                    used(address, i, Type::Address)?;
                }
                Op::Store { ty, address, value } => {
                    if !instruction.results.is_empty() {
                        return Err(Why::WrongCount);
                    }
                    used(address, i, Type::Address)?;
                    used(value, i, *ty)?;
                }
                Op::System {
                    call: System::Write(_),
                    args,
                } => {
                    let [address, count] = &args[..] else {
                        return Err(Why::WrongCount);
                    };
                    if !instruction.results.is_empty() {
                        return Err(Why::WrongCount);
                    }
                    used(address, i, Type::Address)?;
                    used(count, i, Type::Int(Bits::B64))?;
                }
                Op::System {
                    call: System::Allocate,
                    args,
                } => {
                    let ([count], [result]) = (&args[..], &instruction.results[..]) else {
                        return Err(Why::WrongCount);
                    };
                    if f.types[result.0] != Type::Address {
                        return Err(Why::WrongType(*result));
                    }
                    used(count, i, Type::Int(Bits::B64))?;
                }
            }
        }
        let at = block.instructions.len();
        let jump = |j: &Jump| -> Result<(), Why> {
            let target = f.blocks.get(j.block).ok_or(Why::Missing)?;
            if j.args.len() != target.params.len() {
                return Err(Why::WrongCount);
            }
            for (a, p) in j.args.iter().zip(&target.params) {
                used(a, at, f.types[p.0])?;
            }
            Ok(())
        };
        match &block.end {
            End::Jump(j) => jump(j)?,
            End::Switch { on, jumps } => {
                if jumps.is_empty() {
                    return Err(Why::WrongCount);
                }
                if !matches!(f.types.get(on.0), Some(Type::Int(_))) {
                    return Err(Why::WrongType(*on));
                }
                used(on, at, f.types[on.0])?;
                for j in jumps {
                    jump(j)?;
                }
            }
            End::Return(values) => {
                if values.len() != f.results.len() {
                    return Err(Why::WrongCount);
                }
                for (v, want) in values.iter().zip(&f.results) {
                    used(v, at, *want)?;
                }
            }
        }
    }
    Ok(())
}

/// For each block, which values are defined on every path that reaches its
/// start, by a block other than itself.
fn reaching(f: &Function, defined: &[Option<Definition>]) -> Result<Vec<Vec<bool>>, Why> {
    let n = f.blocks.len();
    let mut edges: Vec<Vec<usize>> = vec![Vec::new(); n];
    for (b, block) in f.blocks.iter().enumerate() {
        let jumps: Vec<&Jump> = match &block.end {
            End::Jump(j) => vec![j],
            End::Switch { jumps, .. } => jumps.iter().collect(),
            End::Return(_) => Vec::new(),
        };
        for j in jumps {
            if j.block >= n {
                return Err(Why::Missing);
            }
            edges[b].push(j.block);
        }
    }
    // What each block defines, for the values that exist.
    let mut own = vec![vec![false; defined.len()]; n];
    for (v, d) in defined.iter().enumerate() {
        if let Some((b, _)) = d {
            own[*b][v] = true;
        }
    }
    // Every block but the first starts as if everything reached it, and the
    // paths into it take away what they do not carry.
    let mut at_start = vec![vec![true; defined.len()]; n];
    at_start[0] = vec![false; defined.len()];
    loop {
        let mut changed = false;
        for b in 0..n {
            let out: Vec<bool> = (at_start[b].iter().zip(&own[b]))
                .map(|(a, o)| *a || *o)
                .collect();
            for &next in &edges[b] {
                if next == 0 {
                    continue;
                }
                for (v, carried) in out.iter().enumerate() {
                    if at_start[next][v] && !carried {
                        at_start[next][v] = false;
                        changed = true;
                    }
                }
            }
        }
        if !changed {
            return Ok(at_start);
        }
    }
}
