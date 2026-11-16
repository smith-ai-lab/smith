//! The scalar form: every program the lowering takes gives a form that is
//! well formed and does what the form every target receives does, a form
//! that is damaged is refused, and every program the lowering cannot take
//! says why, and is counted.

use std::collections::BTreeMap;
use std::sync::OnceLock;

use smith::scalar::{self, Bits, Constant, End, Op, Type, Value, Why};
use smith::{eval, ir, lower, lower_scalar};

use crate::Test;
use crate::choices;
use crate::enums;
use crate::generate::{self, Gen, sample};
use crate::lambdas;
use crate::numbers;
use crate::programs;
use crate::reprs;
use crate::text;
use crate::traits;
use crate::tries;
use crate::tuples;

pub const ONCE: &[Test] = &[
    (
        "every_program_is_lowered_to_the_scalar_form_or_says_what_stopped_it",
        every_program_is_lowered_to_the_scalar_form_or_says_what_stopped_it,
    ),
    (
        "every_lowered_program_is_well_formed",
        every_lowered_program_is_well_formed,
    ),
    (
        "the_scalar_form_prints_what_the_form_every_target_receives_prints",
        the_scalar_form_prints_what_the_form_every_target_receives_prints,
    ),
    (
        "a_lowered_program_reads_and_writes_no_address",
        a_lowered_program_reads_and_writes_no_address,
    ),
    (
        "a_text_is_an_address_and_a_length_and_a_bool_is_one_byte",
        a_text_is_an_address_and_a_length_and_a_bool_is_one_byte,
    ),
    (
        "a_struct_a_tuple_and_an_enum_are_their_parts",
        a_struct_a_tuple_and_an_enum_are_their_parts,
    ),
    (
        "an_enum_has_the_places_its_cases_share_by_type",
        an_enum_has_the_places_its_cases_share_by_type,
    ),
    (
        "a_program_that_holds_a_number_says_so",
        a_program_that_holds_a_number_says_so,
    ),
    (
        "a_value_defined_twice_is_refused",
        a_value_defined_twice_is_refused,
    ),
    (
        "a_value_used_where_its_definition_does_not_reach_is_refused",
        a_value_used_where_its_definition_does_not_reach_is_refused,
    ),
    (
        "a_value_used_at_another_type_is_refused",
        a_value_used_at_another_type_is_refused,
    ),
    (
        "a_jump_that_gives_the_wrong_parameters_is_refused",
        a_jump_that_gives_the_wrong_parameters_is_refused,
    ),
];

/// A generated program, in the form every target receives, and in the
/// scalar form or what stopped it.
struct Lowered {
    program: ir::Program,
    scalar: Result<scalar::Program, scalar::Stopped>,
}

/// How many programs each generator contributes.
const CASES: u64 = 40;

/// Every program of every generator, lowered.
fn lowered() -> &'static [Lowered] {
    static LOWERED: OnceLock<Vec<Lowered>> = OnceLock::new();
    LOWERED.get_or_init(|| {
        let mut sources: Vec<Vec<u8>> = Vec::new();
        for case in 0..CASES {
            sources.push(sample(3, case, programs::program).source);
            let (shared, copied) = sample(13, case, programs::shared_and_copied);
            sources.push(shared.into_bytes());
            sources.push(copied.into_bytes());
            sources.push(sample(16, case, traits::boxes).0.into_bytes());
            sources.push(sample(18, case, traits::program).source.into_bytes());
            sources.push(sample(25, case, enums::program).source.into_bytes());
            sources.push(sample(28, case, tuples::program).source.into_bytes());
            sources.push(sample(32, case, lambdas::program).source.into_bytes());
            sources.push(sample(37, case, text::program).source.into_bytes());
            sources.push(sample(43, case, tries::program).source.into_bytes());
            sources.push(sample(64, case, reprs::program).source.into_bytes());
            sources.push(sample(68, case, choices::program).source.into_bytes());
            sources.push(sample(74, case, numbers::program).source.into_bytes());
        }
        sources.into_iter().map(|s| lowered_from(&s)).collect()
    })
}

fn lowered_from(source: &[u8]) -> Lowered {
    let program = lower(source).unwrap_or_else(|e| panic!("{e:?}"));
    let scalar = lower_scalar(source).unwrap_or_else(|e| panic!("{e:?}"));
    Lowered { program, scalar }
}

/// The programs the lowering took, in the scalar form.
fn taken() -> Vec<(&'static Lowered, &'static scalar::Program)> {
    (lowered().iter())
        .filter_map(|l| l.scalar.as_ref().ok().map(|s| (l, s)))
        .collect()
}

fn every_program_is_lowered_to_the_scalar_form_or_says_what_stopped_it() {
    let mut stopped: BTreeMap<&str, usize> = BTreeMap::new();
    for l in lowered() {
        if let Err(scalar::Stopped(feature)) = &l.scalar {
            assert!(!feature.is_empty(), "a program stopped with no feature");
            *stopped.entry(feature).or_default() += 1;
        }
    }
    let taken = taken().len();
    println!(
        "scalar form: {taken} of {} generated programs lowered, {} stopped",
        lowered().len(),
        lowered().len() - taken
    );
    for (feature, count) in &stopped {
        println!("scalar form: {count} stopped by {feature}");
    }
    assert!(taken > 0, "no generated program is lowered");
}

fn every_lowered_program_is_well_formed() {
    for (l, s) in taken() {
        if let Err(malformed) = scalar::check(s) {
            panic!("{malformed:?} in the form of {:?}", l.program);
        }
    }
}

fn the_scalar_form_prints_what_the_form_every_target_receives_prints() {
    for (l, s) in taken() {
        assert_eq!(scalar::run(s), eval::run(&l.program), "{:?}", l.program);
    }
}

fn a_lowered_program_reads_and_writes_no_address() {
    // The only instructions are constants, calls, and calls into the
    // system, and the system's call is given the addresses of texts, never
    // an address the program computed.
    for (_, s) in taken() {
        for f in &s.functions {
            for block in &f.blocks {
                for instruction in &block.instructions {
                    if let Op::System { args, .. } = &instruction.op {
                        assert_eq!(f.types[args[0].0], Type::Address);
                    }
                }
            }
        }
    }
}

/// Lowers a program, and gives the types of the parameters of the second
/// function it holds, which is the first that `main` calls.
fn parameters_of_the_first_call(source: &str) -> Vec<Type> {
    let l = lowered_from(source.as_bytes());
    let s = l.scalar.unwrap_or_else(|e| panic!("{e:?}"));
    assert_eq!(scalar::check(&s), Ok(()));
    let f = &s.functions[1];
    f.blocks[0].params.iter().map(|v| f.types[v.0]).collect()
}

const ADDRESS: Type = Type::Address;
const LENGTH: Type = Type::Int(Bits::B64);
const BYTE: Type = Type::Int(Bits::B8);

fn a_text_is_an_address_and_a_length_and_a_bool_is_one_byte() {
    let text = "fn say(s: str) ! console { print(s) }\nfn main() ! console { say(\"hi\") }\n";
    assert_eq!(parameters_of_the_first_call(text), [ADDRESS, LENGTH]);
    let truth = "fn say(b: bool) ! console { if b { print(\"yes\") } else { print(\"no\") } }\n\
                 fn main() ! console { say(true) }\n";
    assert_eq!(parameters_of_the_first_call(truth), [BYTE]);
}

fn a_struct_a_tuple_and_an_enum_are_their_parts() {
    let structure = "struct Named {\n name: str\n loud: bool\n}\n\
        fn say(n: Named) ! console { print(n.name) }\n\
        fn main() ! console { say(Named(name: \"hi\", loud: true)) }\n";
    assert_eq!(
        parameters_of_the_first_call(structure),
        [ADDRESS, LENGTH, BYTE]
    );
    let tuple = "fn say(pair: (str, bool)) ! console { (s, b) = pair\n print(s) }\n\
        fn main() ! console { say((\"hi\", true)) }\n";
    assert_eq!(parameters_of_the_first_call(tuple), [ADDRESS, LENGTH, BYTE]);
    let enumeration = "enum Shade {\n light\n dark\n}\n\
        fn say(s: Shade) ! console { match s {\n .light { print(\"l\") }\n .dark { print(\"d\") }\n } }\n\
        fn main() ! console { say(.dark) }\n";
    assert_eq!(parameters_of_the_first_call(enumeration), [BYTE]);
    let holding = "enum Maybe {\n some: str\n none\n}\n\
        fn say(m: Maybe) ! console { match m {\n .some(s) { print(s) }\n .none { print(\"none\") }\n } }\n\
        fn main() ! console { say(.some(\"x\")) }\n";
    assert_eq!(
        parameters_of_the_first_call(holding),
        [BYTE, ADDRESS, LENGTH]
    );
}

fn a_program_that_holds_a_number_says_so() {
    let source = "fn main() ! console, alloc { x: i32 = 1\n print(\"{x}\") }\n";
    let l = lowered_from(source.as_bytes());
    assert_eq!(l.scalar.unwrap_err().0, "a number");
}

/// How many programs were damaged, for a test to say it damaged some.
fn damaged_some(count: usize, how: &str) {
    assert!(count > 0, "no generated program could be damaged by {how}");
}

/// Applies the damage to a copy of each form that it fits, and checks that
/// the check refuses the copy, for the reason `refused` says. It gives how
/// many it damaged.
fn damaging(
    damage: impl Fn(&mut scalar::Program) -> bool,
    refused: impl Fn(&Why) -> bool,
) -> usize {
    let mut damaged = 0;
    for (_, s) in taken() {
        let mut copy = s.clone();
        if !damage(&mut copy) {
            continue;
        }
        damaged += 1;
        let Err(malformed) = scalar::check(&copy) else {
            panic!("a damaged form is accepted: {copy:?}");
        };
        assert!(refused(&malformed.why), "{malformed:?} in {copy:?}");
    }
    damaged
}

fn a_value_defined_twice_is_refused() {
    let damaged = damaging(
        |p| {
            for f in &mut p.functions {
                for block in &mut f.blocks {
                    if let Some(at) = block
                        .instructions
                        .iter()
                        .position(|i| !i.results.is_empty())
                    {
                        let again = block.instructions[at].clone();
                        block.instructions.insert(at, again);
                        return true;
                    }
                }
            }
            false
        },
        |why| matches!(why, Why::DefinedTwice(_)),
    );
    damaged_some(damaged, "defining a value twice");
}

/// Whether a block can be reached from another, by the jumps between them.
fn reaches(f: &scalar::Function, from: usize, to: usize) -> bool {
    let mut seen = vec![false; f.blocks.len()];
    let mut stack = vec![from];
    while let Some(b) = stack.pop() {
        if b == to {
            return true;
        }
        if std::mem::replace(&mut seen[b], true) {
            continue;
        }
        match &f.blocks[b].end {
            End::Jump(j) => stack.push(j.block),
            End::Switch { jumps, .. } => stack.extend(jumps.iter().map(|j| j.block)),
            End::Return(_) => {}
        }
    }
    false
}

fn a_value_used_where_its_definition_does_not_reach_is_refused() {
    // Before its definition, in the block that defines it: an instruction
    // that uses a value of the block moves above the one that gives it.
    let before = damaging(
        |p| {
            for f in &mut p.functions {
                for block in &mut f.blocks {
                    for (j, user) in block.instructions.iter().enumerate() {
                        let used = match &user.op {
                            Op::Call { args, .. } | Op::System { args, .. } => args.clone(),
                            Op::Constant(_) => Vec::new(),
                        };
                        let defined = block.instructions[..j]
                            .iter()
                            .position(|i| i.results.iter().any(|r| used.contains(r)));
                        if let Some(k) = defined {
                            let user = block.instructions.remove(j);
                            block.instructions.insert(k, user);
                            return true;
                        }
                    }
                }
            }
            false
        },
        |why| matches!(why, Why::NotReached(_)),
    );
    damaged_some(before, "using a value before its definition");
    // In another block: a block that is not reached from the block of a
    // value's definition is given that value in place of one of the same
    // type.
    let elsewhere = damaging(
        |p| {
            for f in &mut p.functions {
                let n = f.blocks.len();
                for d in 0..n {
                    let defined: Vec<Value> = (f.blocks[d].instructions.iter())
                        .flat_map(|i| i.results.clone())
                        .collect();
                    {
                        for v in defined {
                            let apart: Vec<usize> =
                                (0..n).filter(|&b| b != d && !reaches(f, d, b)).collect();
                            for b in apart {
                                let ty = f.types[v.0];
                                let types = f.types.clone();
                                let replace = |args: &mut Vec<Value>| -> bool {
                                    match args.iter_mut().find(|a| types[a.0] == ty) {
                                        Some(a) => {
                                            *a = v;
                                            true
                                        }
                                        None => false,
                                    }
                                };
                                let block = &mut f.blocks[b];
                                let done =
                                    match &mut block.end {
                                        End::Jump(j) => replace(&mut j.args),
                                        End::Return(values) => replace(values),
                                        End::Switch { .. } => false,
                                    } || block.instructions.iter_mut().any(|i| match &mut i.op {
                                        Op::Call { args, .. } | Op::System { args, .. } => {
                                            replace(args)
                                        }
                                        Op::Constant(_) => false,
                                    });
                                if done {
                                    return true;
                                }
                            }
                        }
                    }
                }
            }
            false
        },
        |why| matches!(why, Why::NotReached(_)),
    );
    damaged_some(
        elsewhere,
        "using a value in a block its definition cannot reach",
    );
}

fn a_value_used_at_another_type_is_refused() {
    let damaged = damaging(
        |p| {
            for f in &mut p.functions {
                for block in &f.blocks {
                    for instruction in &block.instructions {
                        if let Op::System { args, .. } = &instruction.op {
                            // The count of a write, which is a 64-bit
                            // integer, becomes an address.
                            f.types[args[1].0] = Type::Address;
                            return true;
                        }
                    }
                }
            }
            false
        },
        |why| matches!(why, Why::WrongType(_)),
    );
    damaged_some(damaged, "using a value at another type");
    // A constant that is given another type than its own.
    let constant = damaging(
        |p| {
            for f in &mut p.functions {
                for block in &f.blocks {
                    for instruction in &block.instructions {
                        if let Op::Constant(c) = &instruction.op {
                            let r = instruction.results[0];
                            f.types[r.0] = match c {
                                Constant::Int(_) => Type::Address,
                                Constant::Null | Constant::Data(_) => Type::Int(Bits::B8),
                            };
                            return true;
                        }
                    }
                }
            }
            false
        },
        |why| matches!(why, Why::WrongType(_)),
    );
    damaged_some(constant, "giving a constant another type");
}

fn a_jump_that_gives_the_wrong_parameters_is_refused() {
    let damaged = damaging(
        |p| {
            for f in &mut p.functions {
                for block in &mut f.blocks {
                    let End::Jump(j) = &mut block.end else {
                        continue;
                    };
                    if j.args.pop().is_none() {
                        // A jump that gives nothing is given something.
                        j.args.push(Value(0));
                    }
                    return true;
                }
            }
            false
        },
        |why| matches!(why, Why::WrongCount),
    );
    damaged_some(damaged, "giving a block the wrong parameters");
}

/// A type of the generated programs.
#[derive(Clone, Debug)]
enum Model {
    Text,
    Bool,
    Tuple(Vec<Model>),
    /// A struct or an enum the program defines, by its place among them.
    Named(usize),
    /// The generic `Opt[T]`, which holds a `T` or nothing.
    Opt(Box<Model>),
}

/// A definition of a generated program, which holds types defined before it.
#[derive(Clone, Debug)]
enum Def {
    Struct(Vec<Model>),
    Enum(Vec<Option<Model>>),
}

#[derive(Debug)]
struct Layouts {
    defs: Vec<Def>,
    /// The type of the parameter and the source that builds a value of it.
    ty: Model,
    source: String,
}

fn model(g: &mut Gen, defs: &[Def], depth: usize) -> Model {
    let choices = if depth == 0 { 2 } else { 5 };
    match g.below(choices) {
        0 => Model::Text,
        1 => Model::Bool,
        2 if !defs.is_empty() => Model::Named(g.below(defs.len())),
        2 | 3 => {
            let items = 2 + g.below(2);
            Model::Tuple((0..items).map(|_| model(g, defs, depth - 1)).collect())
        }
        _ => Model::Opt(Box::new(model(g, defs, depth - 1))),
    }
}

fn written(m: &Model) -> String {
    match m {
        Model::Text => "str".to_string(),
        Model::Bool => "bool".to_string(),
        Model::Tuple(items) => {
            let items: Vec<String> = items.iter().map(written).collect();
            format!("({})", items.join(", "))
        }
        Model::Named(i) => format!("D{i}"),
        Model::Opt(inner) => format!("Opt[{}]", written(inner)),
    }
}

/// An expression that builds a value of the type.
fn built(g: &mut Gen, defs: &[Def], m: &Model) -> String {
    match m {
        Model::Text => "\"x\"".to_string(),
        Model::Bool => if g.one_in(2) { "true" } else { "false" }.to_string(),
        Model::Tuple(items) => {
            let items: Vec<String> = items.iter().map(|t| built(g, defs, t)).collect();
            format!("({})", items.join(", "))
        }
        Model::Named(i) => match &defs[*i] {
            Def::Struct(fields) => {
                let fields: Vec<String> = (fields.iter().enumerate())
                    .map(|(f, t)| format!("f{f}: {}", built(g, defs, t)))
                    .collect();
                format!("D{i}({})", fields.join(", "))
            }
            Def::Enum(cases) => {
                let c = g.below(cases.len());
                match &cases[c] {
                    Some(t) => format!("D{i}.c{c}({})", built(g, defs, t)),
                    None => format!("D{i}.c{c}"),
                }
            }
        },
        Model::Opt(inner) => format!("Opt.some({})", built(g, defs, inner)),
    }
}

fn layouts(g: &mut Gen) -> Layouts {
    let mut defs: Vec<Def> = Vec::new();
    for _ in 0..1 + g.below(4) {
        let def = if g.one_in(3) {
            Def::Struct((0..1 + g.below(3)).map(|_| model(g, &defs, 2)).collect())
        } else {
            let cases = 1 + g.below(4);
            Def::Enum(
                (0..cases)
                    .map(|_| (!g.one_in(3)).then(|| model(g, &defs, 2)))
                    .collect(),
            )
        };
        defs.push(def);
    }
    let ty = model(g, &defs, 3);
    let mut source = String::from("enum Opt[T] {\n  some: T\n  none\n}\n");
    for (i, def) in defs.iter().enumerate() {
        match def {
            Def::Struct(fields) => {
                source += &format!("struct D{i} {{\n");
                for (f, t) in fields.iter().enumerate() {
                    source += &format!("  f{f}: {}\n", written(t));
                }
            }
            Def::Enum(cases) => {
                source += &format!("enum D{i} {{\n");
                for (c, t) in cases.iter().enumerate() {
                    match t {
                        Some(t) => source += &format!("  c{c}: {}\n", written(t)),
                        None => source += &format!("  c{c}\n"),
                    }
                }
            }
        }
        source += "}\n";
    }
    let value = built(g, &defs, &ty);
    source += &format!(
        "fn consume(v: {}) ! console {{\n  print(\"consumed\")\n}}\n\
         fn main() ! console {{\n  v: {} = {value}\n  consume(v)\n}}\n",
        written(&ty),
        written(&ty)
    );
    Layouts { defs, ty, source }
}

/// The scalars of a value of the type, as the rule says: an address and a
/// length for a text, a byte for the cases of a `bool`, the parts one after
/// another for a struct and a tuple, and for an enum the number of its case
/// in one byte and then, for each scalar type in the order it first appears
/// among the cases' scalars, as many places as the case that needs the most.
fn scalars_of(defs: &[Def], m: &Model) -> Vec<Type> {
    match m {
        Model::Text => vec![ADDRESS, LENGTH],
        Model::Bool => vec![BYTE],
        Model::Tuple(items) => items.iter().flat_map(|t| scalars_of(defs, t)).collect(),
        Model::Named(i) => match &defs[*i] {
            Def::Struct(fields) => fields.iter().flat_map(|t| scalars_of(defs, t)).collect(),
            Def::Enum(cases) => {
                let held: Vec<Vec<Type>> = (cases.iter())
                    .map(|c| c.as_ref().map_or(Vec::new(), |t| scalars_of(defs, t)))
                    .collect();
                places_of(&held)
            }
        },
        Model::Opt(inner) => places_of(&[scalars_of(defs, inner), Vec::new()]),
    }
}

fn places_of(held: &[Vec<Type>]) -> Vec<Type> {
    let mut kinds: Vec<Type> = Vec::new();
    for case in held {
        for t in case {
            if !kinds.contains(t) {
                kinds.push(*t);
            }
        }
    }
    let mut places = vec![BYTE];
    for kind in kinds {
        let most = held
            .iter()
            .map(|case| case.iter().filter(|t| **t == kind).count())
            .max()
            .unwrap_or(0);
        places.extend(std::iter::repeat_n(kind, most));
    }
    places
}

fn an_enum_has_the_places_its_cases_share_by_type() {
    generate::check(80, 150, layouts, |case| {
        let l = lowered_from(case.source.as_bytes());
        let s = l
            .scalar
            .unwrap_or_else(|e| panic!("{e:?} in {}", case.source));
        assert_eq!(scalar::check(&s), Ok(()), "{}", case.source);
        // The parameter of the first function `main` calls.
        let f = &s.functions[1];
        let params: Vec<Type> = f.blocks[0].params.iter().map(|v| f.types[v.0]).collect();
        assert_eq!(params, scalars_of(&case.defs, &case.ty), "{}", case.source);
        assert_eq!(scalar::run(&s), eval::run(&l.program), "{}", case.source);
    });
}
