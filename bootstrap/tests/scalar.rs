//! The scalar form: every program gives a form that is well formed and does
//! what the generators say it prints, a form that is damaged is
//! refused, and what a program needs at run time is written in the form.

use std::sync::OnceLock;

use smith::lower;
use smith::scalar::runtime::{self, Runtime};
use smith::scalar::{self, Bits, Builder, Constant, End, Op, Type, Value, Why};

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
        "every_lowered_program_is_well_formed",
        every_lowered_program_is_well_formed,
    ),
    (
        "the_scalar_form_prints_what_the_generators_say",
        the_scalar_form_prints_what_the_generators_say,
    ),
    (
        "a_texts_bytes_are_read_and_written_as_8_bit_values",
        a_texts_bytes_are_read_and_written_as_8_bit_values,
    ),
    (
        "a_read_or_a_write_that_is_not_through_an_address_or_not_of_its_type_is_refused",
        a_read_or_a_write_that_is_not_through_an_address_or_not_of_its_type_is_refused,
    ),
    (
        "a_program_ends_with_a_failing_status_when_the_system_refuses_memory",
        a_program_ends_with_a_failing_status_when_the_system_refuses_memory,
    ),
    (
        "joining_texts_gives_the_texts_one_after_another",
        joining_texts_gives_the_texts_one_after_another,
    ),
    (
        "writing_a_text_as_a_string_literal_escapes_what_must_be",
        writing_a_text_as_a_string_literal_escapes_what_must_be,
    ),
    (
        "writing_an_integer_gives_its_digits",
        writing_an_integer_gives_its_digits,
    ),
    (
        "two_texts_are_equal_when_they_have_the_same_bytes",
        two_texts_are_equal_when_they_have_the_same_bytes,
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
        "a_number_is_one_scalar_of_the_size_of_its_type",
        a_number_is_one_scalar_of_the_size_of_its_type,
    ),
    (
        "a_program_that_holds_numbers_and_builds_no_text_is_lowered_and_prints_what_it_should",
        a_program_that_holds_numbers_and_builds_no_text_is_lowered_and_prints_what_it_should,
    ),
    (
        "a_program_that_prints_a_number_is_lowered_and_prints_it",
        a_program_that_prints_a_number_is_lowered_and_prints_it,
    ),
    (
        "every_operation_on_numbers_gives_what_arithmetic_on_128_bits_gives",
        every_operation_on_numbers_gives_what_arithmetic_on_128_bits_gives,
    ),
    (
        "an_instruction_on_numbers_used_at_a_wrong_size_is_refused",
        an_instruction_on_numbers_used_at_a_wrong_size_is_refused,
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

/// The values an instruction reads.
fn operands(op: &mut Op) -> Vec<&mut Value> {
    match op {
        Op::Constant(_) => Vec::new(),
        Op::Binary { a, b, .. } | Op::Compare { a, b, .. } => vec![a, b],
        Op::Widen { value, .. } | Op::Narrow(value) => vec![value],
        Op::Choose {
            on,
            then,
            otherwise,
        } => vec![on, then, otherwise],
        Op::Load { address, .. } => vec![address],
        Op::Store { address, value, .. } => vec![address, value],
        Op::Call { args, .. } | Op::System { args, .. } => args.iter_mut().collect(),
    }
}

/// Gives the first value the instruction reads that has the type, if it
/// reads one, the value `v`, and says whether it did.
fn replace_one(op: &mut Op, types: &[Type], ty: Type, v: Value) -> bool {
    match operands(op).into_iter().find(|a| types[a.0] == ty) {
        Some(a) => {
            *a = v;
            true
        }
        None => false,
    }
}

/// A generated program in the scalar form, and what the generator says it
/// prints, when it says.
struct Lowered {
    scalar: scalar::Program,
    stdout: Option<Vec<u8>>,
}

/// How many programs each generator contributes.
const CASES: u64 = 40;

/// Every program of every generator, lowered.
fn lowered() -> &'static [Lowered] {
    static LOWERED: OnceLock<Vec<Lowered>> = OnceLock::new();
    LOWERED.get_or_init(|| {
        let mut sources: Vec<(Vec<u8>, Option<Vec<u8>>)> = Vec::new();
        for case in 0..CASES {
            let c = sample(3, case, programs::program);
            sources.push((c.source, Some(c.stdout)));
            let (shared, copied) = sample(13, case, programs::shared_and_copied);
            sources.push((shared.into_bytes(), None));
            sources.push((copied.into_bytes(), None));
            let (boxes, stdout) = sample(16, case, traits::boxes);
            sources.push((boxes.into_bytes(), Some(stdout)));
            let c = sample(18, case, traits::program);
            sources.push((c.source.into_bytes(), Some(c.stdout)));
            let c = sample(25, case, enums::program);
            sources.push((c.source.into_bytes(), Some(c.stdout)));
            let c = sample(28, case, tuples::program);
            sources.push((c.source.into_bytes(), Some(c.stdout)));
            let c = sample(32, case, lambdas::program);
            sources.push((c.source.into_bytes(), Some(c.stdout)));
            let c = sample(37, case, text::program);
            sources.push((c.source.into_bytes(), Some(c.stdout)));
            let c = sample(43, case, tries::program);
            sources.push((c.source.into_bytes(), Some(c.stdout)));
            let c = sample(64, case, reprs::program);
            sources.push((c.source.into_bytes(), Some(c.stdout)));
            let c = sample(68, case, choices::program);
            sources.push((c.source.into_bytes(), Some(c.stdout)));
            let c = sample(74, case, numbers::program);
            sources.push((c.source.into_bytes(), Some(c.stdout)));
            let c = sample(75, case, numbers::quiet);
            sources.push((c.source.into_bytes(), Some(c.stdout)));
        }
        sources
            .into_iter()
            .map(|(source, stdout)| lowered_from(&source, stdout))
            .collect()
    })
}

fn lowered_from(source: &[u8], stdout: Option<Vec<u8>>) -> Lowered {
    let scalar = lower(source).unwrap_or_else(|e| panic!("{e:?}"));
    Lowered { scalar, stdout }
}

/// Every generated program, in the scalar form.
fn taken() -> Vec<(&'static Lowered, &'static scalar::Program)> {
    lowered().iter().map(|l| (l, &l.scalar)).collect()
}

fn every_lowered_program_is_well_formed() {
    for (l, s) in taken() {
        if let Err(malformed) = scalar::check(s) {
            panic!("{malformed:?} in {:?}", l.scalar);
        }
    }
}

fn the_scalar_form_prints_what_the_generators_say() {
    for (l, s) in taken() {
        if let Some(stdout) = &l.stdout {
            let ran = scalar::run(s);
            assert_eq!(ran.stdout, *stdout, "{s:?}");
            assert_eq!(ran.status, 0, "{s:?}");
        }
    }
}

/// Lowers a program, and gives the types of the parameters of the second
/// function it holds, which is the first that `main` calls.
fn parameters_of_the_first_call(source: &str) -> Vec<Type> {
    let s = lowered_from(source.as_bytes(), None).scalar;
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

/// The scalar type of a whole number type: an integer of its size.
fn size_of(int: numbers::Int) -> Type {
    Type::Int(match int.bits {
        8 => Bits::B8,
        16 => Bits::B16,
        32 => Bits::B32,
        _ => Bits::B64,
    })
}

fn a_number_is_one_scalar_of_the_size_of_its_type() {
    for int in numbers::INTS {
        let source = format!(
            "fn say(n: {0}) ! console {{ print(\"x\") }}\nfn main() ! console {{ say(1) }}\n",
            int.name
        );
        assert_eq!(parameters_of_the_first_call(&source), [size_of(*int)]);
    }
}

fn a_program_that_holds_numbers_and_builds_no_text_is_lowered_and_prints_what_it_should() {
    let held = (0..CASES).map(|case| sample(75, case, numbers::quiet));
    for case in held {
        let s = lowered_from(case.source.as_bytes(), None).scalar;
        assert_eq!(scalar::check(&s), Ok(()), "{}", case.source);
        let ran = scalar::run(&s);
        assert_eq!(ran.stdout, case.stdout, "{}", case.source);
        assert_eq!(ran.status, 0, "{}", case.source);
    }
}

fn a_program_that_prints_a_number_is_lowered_and_prints_it() {
    let source = "fn main() ! console, alloc { x: i32 = -12\n print(\"{x}\") }\n";
    let s = lowered_from(source.as_bytes(), None).scalar;
    assert_eq!(scalar::check(&s), Ok(()));
    assert_eq!(scalar::run(&s).stdout, b"-12\n");
}

/// The operations a function of the generated programs for a type does,
/// each as the source of the function that does it.
fn operations(ty: &str) -> Vec<(String, String, String)> {
    let mut all = Vec::new();
    for (name, op) in [
        ("equal", "=="),
        ("unequal", "!="),
        ("below", "<"),
        ("at_most", "<="),
        ("above", ">"),
        ("at_least", ">="),
    ] {
        let body = format!("a {op} b");
        all.push((name.to_string(), "bool".to_string(), body));
    }
    for kind in ["wrapping", "saturating", "checked"] {
        for op in ["add", "sub", "mul"] {
            let gives = if kind == "checked" {
                format!("Option[{ty}]")
            } else {
                ty.to_string()
            };
            all.push((format!("{kind}_{op}"), gives, format!("{kind}_{op}(a, b)")));
        }
    }
    all
}

/// The number the bits stand for, in the type.
fn number_of(int: numbers::Int, bits: u64) -> i128 {
    if int.signed {
        let up = 64 - int.bits;
        (((bits << up) as i64) >> up) as i128
    } else {
        bits as i128
    }
}

fn every_operation_on_numbers_gives_what_arithmetic_on_128_bits_gives() {
    for int in numbers::INTS {
        let ops = operations(int.name);
        let mut source = String::new();
        for (i, (_, gives, body)) in ops.iter().enumerate() {
            source += &format!(
                "fn op{i}(a: {0}, b: {0}) -> {gives} {{ {body} }}\n",
                int.name
            );
        }
        source += "fn main() {\n";
        for i in 0..ops.len() {
            source += &format!("  r{i} = op{i}(1, 2)\n");
        }
        source += "}\n";
        let s = lowered_from(source.as_bytes(), None).scalar;
        assert_eq!(scalar::check(&s), Ok(()), "{source}");
        // `main` calls each in order, so they are the functions after it.
        let values: Vec<i128> = sample(76, int.bits as u64, |g| {
            let mut all = vec![int.min(), int.max()];
            all.extend((0..24).map(|_| numbers::value(g, *int)));
            all
        });
        let mask = if int.bits == 64 {
            u64::MAX
        } else {
            (1 << int.bits) - 1
        };
        for &a in &values {
            for &b in &values {
                let args = [a as u64 & mask, b as u64 & mask];
                for (i, (name, _, _)) in ops.iter().enumerate() {
                    let given = scalar::call(&s, 1 + i, &args);
                    let (expected, got) = if i < 6 {
                        let op = ["==", "!=", "<", "<=", ">", ">="][i];
                        (
                            numbers::compare(op, a, b).to_string(),
                            ["false", "true"][given[0] as usize].to_string(),
                        )
                    } else {
                        let (kind, op) = name.split_once('_').unwrap();
                        let expected = numbers::arithmetic(kind, op, *int, a, b);
                        let got = match (kind, &given[..]) {
                            // The case `some` is the first of an `Option`.
                            ("checked", [0, n]) => format!(".some({})", number_of(*int, *n)),
                            ("checked", _) => ".none".to_string(),
                            (_, [n]) => number_of(*int, *n).to_string(),
                            _ => panic!("{given:?}"),
                        };
                        (expected, got)
                    };
                    assert_eq!(got, expected, "{name} of {a} and {b} as {}", int.name);
                }
            }
        }
    }
}

/// Damages the first instruction `of` finds, by giving the value `which`
/// picks the other size than it has, and checks that the form is refused.
fn resized(
    of: impl Fn(&Op) -> bool,
    which: impl Fn(&Op) -> Value,
    size: impl Fn(Type) -> Type,
) -> usize {
    damaging(
        |p| {
            for f in &mut p.functions {
                for block in &f.blocks {
                    for instruction in &block.instructions {
                        if of(&instruction.op) {
                            let v = which(&instruction.op);
                            f.types[v.0] = size(f.types[v.0]);
                            return true;
                        }
                    }
                }
            }
            false
        },
        |why| matches!(why, Why::WrongType(_)),
    )
}

fn an_instruction_on_numbers_used_at_a_wrong_size_is_refused() {
    let other = |t: Type| {
        if t == Type::Int(Bits::B8) {
            Type::Int(Bits::B16)
        } else {
            Type::Int(Bits::B8)
        }
    };
    let widest = |_: Type| Type::Int(Bits::B64);
    let narrowest = |_: Type| Type::Int(Bits::B8);
    let operand = |op: &Op| match op {
        Op::Binary { a, .. } | Op::Compare { a, .. } => *a,
        Op::Widen { value, .. } | Op::Narrow(value) => *value,
        Op::Choose { then, .. } => *then,
        _ => unreachable!(),
    };
    let damaged = [
        (
            "arithmetic on numbers of two sizes",
            resized(|o| matches!(o, Op::Binary { .. }), operand, other),
        ),
        (
            "a comparison of numbers of two sizes",
            resized(|o| matches!(o, Op::Compare { .. }), operand, other),
        ),
        (
            "a widening of a number that is not narrower",
            resized(|o| matches!(o, Op::Widen { .. }), operand, widest),
        ),
        (
            "a narrowing of a number that is not wider",
            resized(|o| matches!(o, Op::Narrow(_)), operand, narrowest),
        ),
        (
            "a choice between values of two sizes",
            resized(|o| matches!(o, Op::Choose { .. }), operand, other),
        ),
        (
            "a choice on a number that is not of 8 bits",
            resized(
                |o| matches!(o, Op::Choose { .. }),
                |o| match o {
                    Op::Choose { on, .. } => *on,
                    _ => unreachable!(),
                },
                other,
            ),
        ),
    ];
    for (how, count) in damaged {
        damaged_some(count, how);
    }
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
                        let used: Vec<Value> = operands(&mut user.op.clone())
                            .into_iter()
                            .map(|v| *v)
                            .collect();
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
                                let done = match &mut block.end {
                                    End::Jump(j) => replace(&mut j.args),
                                    End::Return(values) => replace(values),
                                    End::Switch { .. } => false,
                                } || block
                                    .instructions
                                    .iter_mut()
                                    .any(|i| replace_one(&mut i.op, &types, ty, v));
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
                        if let Op::System {
                            call: scalar::System::Write(_),
                            args,
                        } = &instruction.op
                        {
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
    /// A whole number, by its place in `numbers::INTS`.
    Int(usize),
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
    let choices = if depth == 0 { 3 } else { 6 };
    match g.below(choices) {
        0 => Model::Text,
        1 => Model::Bool,
        2 => Model::Int(g.below(numbers::INTS.len())),
        3 if !defs.is_empty() => Model::Named(g.below(defs.len())),
        3 | 4 => {
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
        Model::Int(i) => numbers::INTS[*i].name.to_string(),
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
        Model::Int(i) => numbers::value(g, numbers::INTS[*i]).to_string(),
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
        Model::Int(i) => vec![size_of(numbers::INTS[*i])],
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
        let s = lowered_from(case.source.as_bytes(), None).scalar;
        assert_eq!(scalar::check(&s), Ok(()), "{}", case.source);
        // The parameter of the first function `main` calls.
        let f = &s.functions[1];
        let params: Vec<Type> = f.blocks[0].params.iter().map(|v| f.types[v.0]).collect();
        assert_eq!(params, scalars_of(&case.defs, &case.ty), "{}", case.source);
    });
}

fn a_texts_bytes_are_read_and_written_as_8_bit_values() {
    let mut moved = 0;
    for (_, s) in taken() {
        for f in &s.functions {
            for block in &f.blocks {
                for instruction in &block.instructions {
                    match &instruction.op {
                        Op::Load { ty, .. } => {
                            assert_eq!(*ty, BYTE);
                            assert_eq!(f.types[instruction.results[0].0], *ty);
                            moved += 1;
                        }
                        Op::Store { ty, value, .. } => {
                            assert_eq!(*ty, BYTE);
                            assert_eq!(f.types[value.0], *ty);
                            moved += 1;
                        }
                        _ => {}
                    }
                }
            }
        }
    }
    assert!(moved > 0, "no generated program reads or writes memory");
}

fn a_read_or_a_write_that_is_not_through_an_address_or_not_of_its_type_is_refused() {
    let wrong = |why: &Why| matches!(why, Why::WrongType(_));
    let not_an_address = |op: &Op| match op {
        Op::Load { address, .. } | Op::Store { address, .. } => *address,
        _ => unreachable!(),
    };
    let damaged = [
        (
            "a read through what is not an address",
            resized(|o| matches!(o, Op::Load { .. }), not_an_address, |_| LENGTH),
        ),
        (
            "a write through what is not an address",
            resized(
                |o| matches!(o, Op::Store { .. }),
                not_an_address,
                |_| LENGTH,
            ),
        ),
        (
            "a write of a value that is not of the type it says",
            resized(
                |o| matches!(o, Op::Store { .. }),
                |o| match o {
                    Op::Store { value, .. } => *value,
                    _ => unreachable!(),
                },
                |_| LENGTH,
            ),
        ),
        (
            "a read that gives another type than it says",
            damaging(
                |p| {
                    for f in &mut p.functions {
                        for block in &mut f.blocks {
                            for instruction in &mut block.instructions {
                                if let Op::Load { ty, .. } = &mut instruction.op {
                                    *ty = LENGTH;
                                    return true;
                                }
                            }
                        }
                    }
                    false
                },
                wrong,
            ),
        ),
    ];
    for (how, count) in damaged {
        damaged_some(count, how);
    }
}

fn a_program_ends_with_a_failing_status_when_the_system_refuses_memory() {
    let mut refused = 0;
    let limits: Vec<usize> = (0..48)
        .chain([64, 100, 128, 256, 1000, 4096, 1 << 16])
        .collect();
    for (l, s) in taken() {
        let all = scalar::run(s);
        if let Some(stdout) = &l.stdout {
            assert_eq!(all.stdout, *stdout, "{:?}", l.scalar);
        }
        for &limit in &limits {
            let ran = scalar::run_with_limit(s, limit);
            if ran.status == 0 {
                assert_eq!(ran, all, "with a limit of {limit}: {:?}", l.scalar);
            } else {
                refused += 1;
                assert!(
                    all.stdout.starts_with(&ran.stdout),
                    "with a limit of {limit}: {:?}",
                    l.scalar
                );
            }
        }
    }
    assert!(refused > 0, "no program was refused memory");
}

/// A program that runs `main` after the function of the runtime and what it
/// calls, which have the places from 1.
fn with_runtime(
    kind: Runtime,
    data: Vec<Vec<u8>>,
    main: impl FnOnce(&mut Builder),
) -> scalar::Program {
    let mut kinds = vec![kind];
    let mut functions = Vec::new();
    while functions.len() < kinds.len() {
        let wanted = kinds[functions.len()];
        let built = runtime::build(wanted, &mut |c| {
            1 + match kinds.iter().position(|k| *k == c) {
                Some(at) => at,
                None => {
                    kinds.push(c);
                    kinds.len() - 1
                }
            }
        });
        functions.push(built);
    }
    let mut b = Builder::new();
    main(&mut b);
    functions.insert(0, b.finish(&[]));
    let program = scalar::Program {
        data,
        functions,
        main: 0,
    };
    assert_eq!(scalar::check(&program), Ok(()));
    program
}

/// The text that is the program's text at the place, as an address and a
/// length.
fn data_text(b: &mut Builder, at: usize, length: usize) -> Vec<Value> {
    let address = b.emit(Op::Constant(Constant::Data(at)), &[ADDRESS])[0];
    vec![address, b.int(Bits::B64, length as u64)]
}

fn write_text(b: &mut Builder, text: Vec<Value>) {
    let call = scalar::System::Write(scalar::Stream::Stdout);
    b.emit(Op::System { call, args: text }, &[]);
}

/// A text of the characters the runtime treats in its own way and some it
/// does not.
fn some_text(g: &mut Gen) -> Vec<u8> {
    const BYTES: &[u8] = b"ab \"\\{}\n\t\0\xff-9";
    (0..g.below(7)).map(|_| *g.pick(BYTES)).collect()
}

fn joining_texts_gives_the_texts_one_after_another() {
    generate::check(
        90,
        200,
        |g| (0..g.below(6)).map(|_| some_text(g)).collect::<Vec<_>>(),
        |texts| {
            let program = with_runtime(Runtime::Join(texts.len()), texts.clone(), |b| {
                let mut args = Vec::new();
                for (at, text) in texts.iter().enumerate() {
                    args.extend(data_text(b, at, text.len()));
                }
                let joined = b.call(1, args, &[ADDRESS, LENGTH]);
                write_text(b, joined);
            });
            let ran = scalar::run(&program);
            assert_eq!((ran.status, ran.stdout), (0, texts.concat()));
        },
    );
}

/// The text written as a string literal, which is worked out here with no
/// help from the runtime.
fn literal(text: &[u8]) -> Vec<u8> {
    let mut out = String::from("\"");
    for &b in text {
        match b {
            b'"' | b'\\' | b'{' | b'}' => out += &format!("\\{}", b as char),
            b'\n' => out += "\\n",
            b'\t' => out += "\\t",
            _ => out.extend(std::iter::once(b as char)),
        }
    }
    out.push('"');
    // Bytes that are not characters of one byte are those above 127.
    out.chars().map(|c| c as u32 as u8).collect()
}

fn writing_a_text_as_a_string_literal_escapes_what_must_be() {
    generate::check(91, 200, some_text, |text| {
        let program = with_runtime(Runtime::Escape, vec![text.clone()], |b| {
            let args = data_text(b, 0, text.len());
            let written = b.call(1, args, &[ADDRESS, LENGTH]);
            write_text(b, written);
        });
        let ran = scalar::run(&program);
        assert_eq!((ran.status, ran.stdout), (0, literal(text)));
    });
}

fn writing_an_integer_gives_its_digits() {
    let generate = |g: &mut Gen| {
        let int = *g.pick(numbers::INTS);
        (int, numbers::value(g, int))
    };
    generate::check(92, 400, generate, |(int, value)| {
        let bits = match int.bits {
            8 => Bits::B8,
            16 => Bits::B16,
            32 => Bits::B32,
            _ => Bits::B64,
        };
        let kind = Runtime::Decimal {
            bits,
            signed: int.signed,
        };
        let program = with_runtime(kind, Vec::new(), |b| {
            let word = if int.bits == 64 {
                *value as u64
            } else {
                *value as u64 & ((1 << int.bits) - 1)
            };
            let n = b.int(bits, word);
            let written = b.call(1, vec![n], &[ADDRESS, LENGTH]);
            write_text(b, written);
        });
        let ran = scalar::run(&program);
        assert_eq!(
            (ran.status, ran.stdout),
            (0, value.to_string().into_bytes()),
            "{}",
            int.name
        );
    });
}

fn two_texts_are_equal_when_they_have_the_same_bytes() {
    let generate = |g: &mut Gen| {
        let a = some_text(g);
        let b = if g.one_in(2) { a.clone() } else { some_text(g) };
        (a, b)
    };
    generate::check(93, 300, generate, |(a, b)| {
        let data = vec![a.clone(), b.clone(), b"0".to_vec(), b"1".to_vec()];
        let program = with_runtime(Runtime::Equal, data, |bld| {
            let args = [data_text(bld, 0, a.len()), data_text(bld, 1, b.len())].concat();
            let same = bld.call(1, args, &[BYTE])[0];
            let (no, yes) = (data_text(bld, 2, 1), data_text(bld, 3, 1));
            let address = bld.choose(same, yes[0], no[0]);
            write_text(bld, vec![address, yes[1]]);
        });
        let ran = scalar::run(&program);
        let expected = if a == b { b"1" } else { b"0" };
        assert_eq!((ran.status, ran.stdout), (0, expected.to_vec()));
    });
}
