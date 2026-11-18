//! The scalar form: every program gives a form that is well formed and does
//! what the generators say it prints, a form that is damaged is
//! refused, and what a program needs at run time is written in the form.

use std::collections::HashMap;
use std::sync::OnceLock;

use smith::lower;
use smith::scalar::runtime::{self, Runtime};
use smith::scalar::{self, Bits, Builder, Constant, End, Op, Stream, System, Type, Value, Why};

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
    (
        "inlining_gives_a_well_formed_program_that_does_what_it_did",
        inlining_gives_a_well_formed_program_that_does_what_it_did,
    ),
    (
        "inlining_leaves_no_call_the_rule_replaces_and_no_function_nothing_calls",
        inlining_leaves_no_call_the_rule_replaces_and_no_function_nothing_calls,
    ),
    (
        "inlining_gives_the_same_program_every_time_and_grows_it_by_a_bounded_amount",
        inlining_gives_the_same_program_every_time_and_grows_it_by_a_bounded_amount,
    ),
    (
        "inlining_replaces_calls_of_small_functions_and_of_functions_called_once",
        inlining_replaces_calls_of_small_functions_and_of_functions_called_once,
    ),
    (
        "the_passes_give_a_well_formed_program_that_does_what_it_did",
        the_passes_give_a_well_formed_program_that_does_what_it_did,
    ),
    (
        "folding_leaves_no_instruction_whose_inputs_are_all_constants",
        folding_leaves_no_instruction_whose_inputs_are_all_constants,
    ),
    (
        "removal_leaves_nothing_that_nothing_uses",
        removal_leaves_nothing_that_nothing_uses,
    ),
    (
        "the_passes_give_the_same_program_every_time_and_never_a_larger_one",
        the_passes_give_the_same_program_every_time_and_never_a_larger_one,
    ),
    (
        "removal_makes_at_most_four_walks_and_leaves_no_work",
        removal_makes_at_most_four_walks_and_leaves_no_work,
    ),
    (
        "removal_makes_as_many_walks_over_ten_thousand_blocks_as_over_ten",
        removal_makes_as_many_walks_over_ten_thousand_blocks_as_over_ten,
    ),
    (
        "the_passes_leave_no_block_that_holds_only_a_jump",
        the_passes_leave_no_block_that_holds_only_a_jump,
    ),
    (
        "a_comparison_only_a_choice_reads_is_the_last_instruction_of_its_block",
        a_comparison_only_a_choice_reads_is_the_last_instruction_of_its_block,
    ),
    (
        "sharing_leaves_no_instruction_that_does_what_an_earlier_one_does",
        sharing_leaves_no_instruction_that_does_what_an_earlier_one_does,
    ),
    (
        "sharing_never_replaces_a_read_through_an_address",
        sharing_never_replaces_a_read_through_an_address,
    ),
    (
        "what_is_worked_out_in_a_block_that_dominates_another_is_not_worked_out_again",
        what_is_worked_out_in_a_block_that_dominates_another_is_not_worked_out_again,
    ),
    (
        "a_jump_to_the_block_that_follows_a_choice_on_a_constant_and_a_copied_value_are_gone",
        a_jump_to_the_block_that_follows_a_choice_on_a_constant_and_a_copied_value_are_gone,
    ),
    (
        "a_value_a_loop_carries_unchanged_is_the_value_it_started_with",
        a_value_a_loop_carries_unchanged_is_the_value_it_started_with,
    ),
    (
        "an_instruction_that_reads_its_own_result_is_refused",
        an_instruction_that_reads_its_own_result_is_refused,
    ),
    (
        "a_widening_or_a_narrowing_to_the_size_it_has_is_refused",
        a_widening_or_a_narrowing_to_the_size_it_has_is_refused,
    ),
    (
        "a_text_or_a_block_the_program_does_not_hold_is_refused",
        a_text_or_a_block_the_program_does_not_hold_is_refused,
    ),
    (
        "a_value_a_loop_passes_through_another_block_unchanged_is_the_value_it_started_with",
        a_value_a_loop_passes_through_another_block_unchanged_is_the_value_it_started_with,
    ),
    (
        "a_function_that_jumps_back_to_its_first_block_is_given_by_the_passes_as_it_was",
        a_function_that_jumps_back_to_its_first_block_is_given_by_the_passes_as_it_was,
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
const CASES: u64 = 8;

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

/// Every function of every generated program, in the scalar form.
pub fn every_function() -> Vec<&'static scalar::Function> {
    taken()
        .into_iter()
        .flat_map(|(_, s)| &s.functions)
        .collect()
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
            // Of two different values: resizing a value that is both operands
            // leaves them the size of each other.
            resized(
                |o| matches!(o, Op::Compare { a, b, .. } if a != b),
                operand,
                other,
            ),
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

/// The first value of a function that an integer constant defines, with the
/// place of its instruction.
fn first_int_constant(f: &scalar::Function) -> Option<(usize, usize, Value)> {
    for (b, block) in f.blocks.iter().enumerate() {
        for (i, instruction) in block.instructions.iter().enumerate() {
            if let (Op::Constant(Constant::Int(_)), [result]) =
                (&instruction.op, &instruction.results[..])
                && matches!(f.types[result.0], Type::Int(_))
            {
                return Some((b, i, *result));
            }
        }
    }
    None
}

fn an_instruction_that_reads_its_own_result_is_refused() {
    let damaged = damaging(
        |p| {
            for f in &mut p.functions {
                let types = f.types.clone();
                for block in &mut f.blocks {
                    for instruction in &mut block.instructions {
                        let [result] = instruction.results[..] else {
                            continue;
                        };
                        if replace_one(&mut instruction.op, &types, types[result.0], result) {
                            return true;
                        }
                    }
                }
            }
            false
        },
        |why| matches!(why, Why::NotReached(_)),
    );
    damaged_some(damaged, "an instruction reading its own result");
}

fn a_widening_or_a_narrowing_to_the_size_it_has_is_refused() {
    for widens in [true, false] {
        let damaged = damaging(
            |p| {
                for f in &mut p.functions {
                    let Some((b, i, value)) = first_int_constant(f) else {
                        continue;
                    };
                    let ty = f.types[value.0];
                    let result = Value(f.types.len());
                    f.types.push(ty);
                    let op = if widens {
                        Op::Widen {
                            signed: false,
                            value,
                        }
                    } else {
                        Op::Narrow(value)
                    };
                    let instruction = scalar::Instruction {
                        results: vec![result],
                        op,
                    };
                    f.blocks[b].instructions.insert(i + 1, instruction);
                    return true;
                }
                false
            },
            |why| matches!(why, Why::WrongType(_)),
        );
        damaged_some(damaged, "a change of an integer to the size it has");
    }
}

fn a_text_or_a_block_the_program_does_not_hold_is_refused() {
    // The first place past the last text.
    let text = damaging(
        |p| {
            let past = p.data.len();
            for f in &mut p.functions {
                for block in &mut f.blocks {
                    for instruction in &mut block.instructions {
                        if let Op::Constant(c @ Constant::Data(_)) = &mut instruction.op {
                            *c = Constant::Data(past);
                            return true;
                        }
                    }
                }
            }
            false
        },
        |why| matches!(why, Why::Missing),
    );
    damaged_some(text, "a text past the last");
    // The first place past the last block.
    let block = damaging(
        |p| {
            for f in &mut p.functions {
                let past = f.blocks.len();
                for block in &mut f.blocks {
                    if let End::Jump(j) = &mut block.end {
                        j.block = past;
                        return true;
                    }
                }
            }
            false
        },
        |why| matches!(why, Why::Missing),
    );
    damaged_some(block, "a jump past the last block");
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
    // The runtime's copy moves a word at a time, as its own test says.
    let copy = runtime::build(Runtime::Copy, &mut |_| 0);
    // The handler's functions move addresses and numbers the size of one.
    let take = scalar::handler::take();
    for (_, s) in taken() {
        let ready = (s.functions.iter().position(|f| *f == take)).map(scalar::handler::ready);
        for f in s
            .functions
            .iter()
            .filter(|f| **f != copy && **f != take && Some(*f) != ready.as_ref())
        {
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
                |t| if t == LENGTH { BYTE } else { LENGTH },
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
                                    *ty = if *ty == LENGTH { BYTE } else { LENGTH };
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
pub fn with_runtime(
    kind: Runtime,
    data: Vec<Vec<u8>>,
    main: impl FnOnce(&mut Builder),
) -> scalar::Program {
    let mut kinds = vec![kind];
    let mut functions = Vec::new();
    while functions.len() < kinds.len() {
        let wanted = kinds[functions.len()].clone();
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

/// Every call in the program: the function that holds it, and the function
/// it calls.
fn calls_of(p: &scalar::Program) -> Vec<(usize, usize)> {
    let mut found = Vec::new();
    for (holder, f) in p.functions.iter().enumerate() {
        for i in f.blocks.iter().flat_map(|b| &b.instructions) {
            if let Op::Call { function, .. } = i.op {
                found.push((holder, function));
            }
        }
    }
    found
}

fn inlining_gives_a_well_formed_program_that_does_what_it_did() {
    let limits = [0, 1, 7, 40, 200, 4096, usize::MAX];
    for (l, s) in taken() {
        let after = scalar::inline(s);
        if let Err(malformed) = scalar::check(&after) {
            panic!("{malformed:?} in {after:?}");
        }
        for limit in limits {
            let before = scalar::run_with_limit(s, limit);
            let ran = scalar::run_with_limit(&after, limit);
            assert_eq!(ran, before, "with a limit of {limit}: {:?}", l.scalar);
        }
    }
}

fn inlining_leaves_no_call_the_rule_replaces_and_no_function_nothing_calls() {
    for (_, s) in taken() {
        let after = scalar::inline(s);
        let calls = calls_of(&after);
        for &(_, callee) in &calls {
            let sites = calls.iter().filter(|(_, c)| *c == callee).count();
            let size = scalar::size(&after.functions[callee]);
            assert!(
                sites > 1 && size > scalar::BOUND,
                "a call of a function with {sites} calls and a size of {size} is left"
            );
        }
        for function in 0..after.functions.len() {
            assert!(
                function == after.main || calls.iter().any(|(_, c)| *c == function),
                "function {function} is called by nothing"
            );
        }
    }
}

fn inlining_gives_the_same_program_every_time_and_grows_it_by_a_bounded_amount() {
    for (_, s) in taken() {
        let after = scalar::inline(s);
        assert_eq!(after, scalar::inline(s));
        let size = |p: &scalar::Program| -> usize { p.functions.iter().map(scalar::size).sum() };
        // Each call is replaced by at most one function's worth of the
        // size the rule allows, so the work is a walk over the program it
        // gives, with nothing repeated until it stops changing.
        let calls = calls_of(s).len();
        assert!(
            size(&after) <= size(s) + calls * scalar::BOUND,
            "{} from {}",
            size(&after),
            size(s)
        );
        assert_eq!(scalar::inline(&after), after);
    }
}

/// A function of the given size that returns its parameter, and a `main`
/// that calls it from as many places as asked.
fn caller_of(callee_size: usize, calls: usize) -> scalar::Program {
    let int = Type::Int(Bits::B64);
    let mut f = Builder::new();
    let [p] = f.params(&[int])[..] else { panic!() };
    let mut last = p;
    for _ in 1..callee_size {
        last = f.binary(scalar::Binary::Add, last, p);
    }
    f.ret(vec![last]);
    let callee = f.finish(&[int]);
    let mut m = Builder::new();
    let one = m.int(Bits::B64, 1);
    for _ in 0..calls {
        m.call(1, vec![one], &[int]);
    }
    m.ret(Vec::new());
    scalar::Program {
        data: Vec::new(),
        functions: vec![m.finish(&[]), callee],
        main: 0,
    }
}

fn inlining_replaces_calls_of_small_functions_and_of_functions_called_once() {
    // A function that is called once is replaced however large.
    let big = scalar::BOUND * 10;
    let once = scalar::inline(&caller_of(big, 1));
    assert!(calls_of(&once).is_empty());
    assert_eq!(once.functions.len(), 1);
    // One that is called more often is replaced only when it is small.
    // A function of size n holds n - 1 instructions and one end.
    let small = scalar::inline(&caller_of(scalar::BOUND, 3));
    assert!(calls_of(&small).is_empty());
    assert_eq!(small.functions.len(), 1);
    let large = scalar::inline(&caller_of(scalar::BOUND + 1, 3));
    assert_eq!(calls_of(&large).len(), 3);
    assert_eq!(large.functions.len(), 2);
    for p in [once, small, large] {
        scalar::check(&p).unwrap();
    }
}

/// The program after each pass, in order, so that each is checked on what
/// the one before it gave.
fn after_each_pass(p: &scalar::Program) -> [scalar::Program; 5] {
    let inlined = scalar::inline(p);
    let trimmed = scalar::trim(&inlined);
    let folded = scalar::fold(&trimmed);
    let shared = scalar::common(&folded);
    let removed = scalar::remove(&shared);
    [inlined, trimmed, folded, shared, removed]
}

fn the_passes_give_a_well_formed_program_that_does_what_it_did() {
    let limits = [0, 1, 7, 40, 200, 4096, usize::MAX];
    for (l, s) in taken() {
        let [inlined, trimmed, folded, shared, removed] = after_each_pass(s);
        for after in [&trimmed, &folded, &shared, &removed] {
            if let Err(malformed) = scalar::check(after) {
                panic!("{malformed:?} in {after:?}");
            }
            for limit in limits {
                let before = scalar::run_with_limit(&inlined, limit);
                let ran = scalar::run_with_limit(after, limit);
                assert_eq!(ran, before, "with a limit of {limit}: {:?}", l.scalar);
            }
        }
        assert_eq!(removed, scalar::passes(s));
    }
}

/// The values the function defines by a constant integer, and what each is.
fn constants_of(f: &scalar::Function) -> Vec<Option<u64>> {
    let mut known = vec![None; f.types.len()];
    for i in f.blocks.iter().flat_map(|b| &b.instructions) {
        if let Op::Constant(Constant::Int(n)) = i.op {
            known[i.results[0].0] = Some(n);
        }
    }
    known
}

/// What is left to fold in the program, as a message for each.
fn left_to_fold(p: &scalar::Program) -> Vec<String> {
    let mut left = Vec::new();
    for (function, f) in p.functions.iter().enumerate() {
        let known = constants_of(f);
        let all = |values: &[Value]| values.iter().all(|v| known[v.0].is_some());
        for (b, block) in f.blocks.iter().enumerate() {
            for i in &block.instructions {
                let folds = match &i.op {
                    Op::Binary { a, b, .. } => {
                        matches!(f.types[i.results[0].0], Type::Int(_)) && all(&[*a, *b])
                    }
                    Op::Compare { a, b, .. } => all(&[*a, *b]),
                    Op::Widen { value, .. } | Op::Narrow(value) => all(&[*value]),
                    Op::Choose { on, .. } => all(&[*on]),
                    _ => false,
                };
                if folds {
                    left.push(format!("function {function} block {b}: {i:?}"));
                }
            }
            if let End::Switch { on, .. } = &block.end
                && all(&[*on])
            {
                left.push(format!(
                    "function {function} block {b} switches on a constant"
                ));
            }
        }
    }
    left
}

fn folding_leaves_no_instruction_whose_inputs_are_all_constants() {
    for (_, s) in taken() {
        let after = scalar::passes(s);
        assert_eq!(left_to_fold(&after), Vec::<String>::new(), "{after:?}");
    }
}

/// What is left in the program that removal takes away, as a message for
/// each.
fn left_to_remove(p: &scalar::Program) -> Vec<String> {
    let mut left = Vec::new();
    for (function, f) in p.functions.iter().enumerate() {
        let mut used = vec![0usize; f.types.len()];
        let mut jumps_into = vec![0usize; f.blocks.len()];
        let mut given: Vec<Vec<Vec<Value>>> = vec![Vec::new(); f.blocks.len()];
        let mut reached = vec![false; f.blocks.len()];
        let mut stack = vec![0];
        reached[0] = true;
        while let Some(b) = stack.pop() {
            for to in f.blocks[b].end.successors() {
                if !reached[to] {
                    reached[to] = true;
                    stack.push(to);
                }
            }
        }
        for (b, block) in f.blocks.iter().enumerate() {
            for i in &block.instructions {
                for v in i.op.operands() {
                    used[v.0] += 1;
                }
            }
            for v in block.end.operands() {
                used[v.0] += 1;
            }
            let jumps: Vec<&scalar::Jump> = match &block.end {
                End::Jump(j) => vec![j],
                End::Switch { jumps, .. } => jumps.iter().collect(),
                End::Return(_) => Vec::new(),
            };
            for j in jumps {
                jumps_into[j.block] += 1;
                given[j.block].push(j.args.clone());
                if let End::Jump(_) = block.end
                    && j.block != 0
                    && j.block != b
                    && !f.blocks[j.block].params.is_empty()
                {
                    // Whether it is joined is told by its count below.
                }
            }
        }
        for (b, block) in f.blocks.iter().enumerate() {
            if !reached[b] {
                left.push(format!(
                    "function {function}: block {b} is reached by nothing"
                ));
            }
            for i in &block.instructions {
                let writes = matches!(i.op, Op::Store { .. } | Op::Call { .. } | Op::System { .. });
                if !writes && i.results.iter().all(|r| used[r.0] == 0) {
                    left.push(format!(
                        "function {function} block {b}: {i:?} is used by nothing"
                    ));
                }
            }
            if b != 0 {
                for (k, p) in block.params.iter().enumerate() {
                    // A parameter is used by what reads it, not by the jump
                    // that gives it on to itself.
                    let own = given[b].iter().filter(|args| args[k] == *p).count();
                    if used[p.0] == own {
                        left.push(format!(
                            "function {function}: block {b} takes {p:?} for nothing"
                        ));
                    }
                    let mut others: Vec<Value> = (given[b].iter())
                        .map(|args| args[k])
                        .filter(|v| v != p)
                        .collect();
                    others.dedup();
                    others.sort();
                    others.dedup();
                    if others.len() == 1 {
                        left.push(format!(
                            "function {function}: block {b} takes {p:?}, which is always {others:?}"
                        ));
                    }
                }
            }
            if let End::Jump(j) = &block.end
                && j.block != 0
                && j.block != b
                && jumps_into[j.block] == 1
            {
                left.push(format!(
                    "function {function}: block {b} jumps alone to block {}",
                    j.block
                ));
            }
        }
    }
    left
}

fn removal_leaves_nothing_that_nothing_uses() {
    for (_, s) in taken() {
        let after = scalar::passes(s);
        assert_eq!(left_to_remove(&after), Vec::<String>::new(), "{after:?}");
    }
}

fn the_passes_give_the_same_program_every_time_and_never_a_larger_one() {
    let size = |p: &scalar::Program| -> usize { p.functions.iter().map(scalar::size).sum() };
    for (_, s) in taken() {
        let [inlined, trimmed, folded, shared, removed] = after_each_pass(s);
        assert_eq!(trimmed, scalar::trim(&inlined));
        assert_eq!(folded, scalar::fold(&trimmed));
        assert_eq!(shared, scalar::common(&folded));
        assert_eq!(removed, scalar::remove(&shared));
        assert!(size(&shared) <= size(&folded), "{shared:?}");
        assert!(size(&removed) <= size(&shared), "{removed:?}");
        assert_eq!(scalar::remove(&removed), removed);
    }
}

/// Removal makes the walks `knowledge/compiler.md` says, at most four over a
/// function, and what it gives a program of the generators has no work left,
/// as another removal finds none.
fn removal_makes_at_most_four_walks_and_leaves_no_work() {
    assert_eq!(scalar::WALKS, 4);
    for (_, s) in taken() {
        let shared = scalar::common(&scalar::fold(&scalar::inline(s)));
        let (removed, walks) = scalar::remove_counting(&shared);
        assert!(walks <= scalar::WALKS, "{walks} walks over {s:?}");
        let (again, more) = scalar::remove_counting(&removed);
        assert_eq!((&again, more), (&removed, 1), "{s:?}");
    }
}

/// A function of about `blocks` blocks in a row that each pass a number on: some
/// add to it, some only jump, some choose between two blocks that only jump on
/// to the same block, on a comparison that nothing else reads, so that each
/// such choice is a jump and its block is one that only jumps once the
/// comparison goes, and some choose between two blocks that add different
/// numbers. The last writes the number.
fn blocks_in_a_row(g: &mut Gen, blocks: usize) -> scalar::Program {
    let long = Type::Int(Bits::B64);
    let mut b = Builder::new();
    let row: Vec<(usize, Value)> = (0..blocks)
        .map(|_| {
            let (id, params) = b.block(&[long]);
            (id, params[0])
        })
        .collect();
    let start = b.int(Bits::B64, 1);
    b.jump(row[0].0, vec![start]);
    for (i, &(id, acc)) in row.iter().enumerate() {
        b.at(id);
        let Some(&(next, _)) = row.get(i + 1) else {
            let eight = b.int(Bits::B64, 8);
            let buffer = b.allocate(eight);
            b.store(buffer, acc);
            let write = Op::System {
                call: System::Write(Stream::Stdout),
                args: vec![buffer, eight],
            };
            b.emit(write, &[]);
            b.ret(vec![]);
            break;
        };
        match g.below(4) {
            0 => {
                let n = b.int(Bits::B64, 1 + g.below(5) as u64);
                let sum = b.binary(scalar::Binary::Add, acc, n);
                b.jump(next, vec![sum]);
            }
            1 => b.jump(next, vec![acc]),
            kind => {
                let n = b.int(Bits::B64, g.below(40) as u64);
                let on = b.compare(scalar::Compare::LessU, acc, n);
                let (left, l) = b.block(&[long]);
                let (right, r) = b.block(&[long]);
                b.switch(on, vec![(left, vec![acc]), (right, vec![acc])]);
                for (arm, value) in [(left, l[0]), (right, r[0])] {
                    b.at(arm);
                    if kind == 3 {
                        let n = b.int(Bits::B64, 1 + g.below(5) as u64);
                        let sum = b.binary(scalar::Binary::Add, value, n);
                        b.jump(next, vec![sum]);
                    } else {
                        b.jump(next, vec![value]);
                    }
                }
            }
        }
    }
    scalar::Program {
        data: Vec::new(),
        functions: vec![b.finish(&[])],
        main: 0,
    }
}

/// Removal makes no more walks over a function of ten thousand blocks than
/// over one of ten, and each gives the same output, has no block that only
/// jumps, and has no work left.
fn removal_makes_as_many_walks_over_ten_thousand_blocks_as_over_ten() {
    let mut most = 0;
    for blocks in [10, 100, 1_000, 10_000] {
        let program = generate::sample(2, blocks as u64, |g| blocks_in_a_row(g, blocks));
        let (removed, walks) = scalar::remove_counting(&program);
        assert!(walks <= scalar::WALKS, "{walks} walks over {blocks} blocks");
        most = most.max(walks);
        assert_eq!(scalar::run(&removed), scalar::run(&program), "{blocks} blocks");
        let f = &removed.functions[0];
        let jumps_only = (f.blocks.iter().enumerate())
            .filter(|(b, block)| {
                *b != 0 && block.instructions.is_empty() && matches!(block.end, End::Jump(_))
            })
            .count();
        assert_eq!(jumps_only, 0, "{blocks} blocks");
        if blocks <= 100 {
            assert_eq!(scalar::check(&removed), Ok(()), "{blocks} blocks");
        }
        assert_eq!(scalar::remove(&removed), removed, "{blocks} blocks");
    }
    assert!(most < scalar::WALKS, "{most} walks for the most blocks");
}

/// How many jumps go to each block of the function.
fn jumps_to(f: &scalar::Function) -> Vec<usize> {
    let mut count = vec![0; f.blocks.len()];
    for block in &f.blocks {
        for to in block.end.successors() {
            count[to] += 1;
        }
    }
    count
}

/// No block holds only a jump, except the first block of a function when
/// the block it jumps to is jumped to from elsewhere too, as a loop's is: the
/// function must start in a block that no jump need leave, and the block
/// that jumps to the loop cannot be joined to what it jumps to.
fn the_passes_leave_no_block_that_holds_only_a_jump() {
    for (_, s) in taken() {
        let after = scalar::passes(s);
        for f in &after.functions {
            let count = jumps_to(f);
            for (b, block) in f.blocks.iter().enumerate() {
                let End::Jump(j) = &block.end else { continue };
                if !block.instructions.is_empty() {
                    continue;
                }
                assert!(
                    b == 0 && count[j.block] > 1,
                    "block {b} holds only a jump in {f:?}"
                );
            }
        }
    }
}

/// A comparison that only the choice that ends its block reads is the last
/// instruction of the block, so that a target finds it where it is used.
fn a_comparison_only_a_choice_reads_is_the_last_instruction_of_its_block() {
    for (_, s) in taken() {
        let after = scalar::passes(s);
        for f in &after.functions {
            let mut reads = vec![0usize; f.types.len()];
            for block in &f.blocks {
                let operands = block.instructions.iter().flat_map(|i| i.op.operands());
                for v in operands.chain(block.end.operands()) {
                    reads[v.0] += 1;
                }
            }
            for block in &f.blocks {
                let End::Switch { on, jumps } = &block.end else {
                    continue;
                };
                let compares = block
                    .instructions
                    .iter()
                    .position(|i| i.results == [*on] && matches!(i.op, Op::Compare { .. }));
                if let (2, 1, Some(at)) = (jumps.len(), reads[on.0], compares) {
                    assert_eq!(at, block.instructions.len() - 1, "{f:?}");
                }
            }
        }
    }
}

/// A program that writes `A` or `B`, by a choice on what arithmetic on
/// constants says, through a block that only the choice reaches and a value
/// that its parameters copy.
fn a_choice_on_constants() -> scalar::Program {
    let byte = Type::Int(Bits::B8);
    let long = Type::Int(Bits::B64);
    let mut m = Builder::new();
    let (after, copied) = m.block(&[byte]);
    let (taken, kept) = m.block(&[byte]);
    let (not_taken, _) = m.block(&[]);
    let one = m.int(Bits::B8, 1);
    let two = m.int(Bits::B8, 2);
    let three = m.int(Bits::B8, 3);
    let sum = m.binary(scalar::Binary::Add, one, two);
    let is_three = m.compare(scalar::Compare::Equal, sum, three);
    let picked = m.choose(is_three, one, two);
    let unused = m.binary(scalar::Binary::Mul, picked, picked);
    let _ = unused;
    m.switch(
        is_three,
        vec![(not_taken, Vec::new()), (taken, vec![picked])],
    );
    m.at(taken);
    let text = m.emit(Op::Constant(Constant::Data(0)), &[Type::Address])[0];
    let count = m.int(Bits::B64, 1);
    m.emit(
        Op::System {
            call: System::Write(Stream::Stdout),
            args: vec![text, count],
        },
        &[],
    );
    m.jump(after, vec![kept[0]]);
    m.at(not_taken);
    let text = m.emit(Op::Constant(Constant::Data(1)), &[Type::Address])[0];
    let count = m.int(Bits::B64, 1);
    m.emit(
        Op::System {
            call: System::Write(Stream::Stdout),
            args: vec![text, count],
        },
        &[],
    );
    m.jump(after, vec![two]);
    m.at(after);
    let _ = (copied, long);
    m.ret(Vec::new());
    scalar::Program {
        data: vec![b"A".to_vec(), b"B".to_vec()],
        functions: vec![m.finish(&[])],
        main: 0,
    }
}

fn a_jump_to_the_block_that_follows_a_choice_on_a_constant_and_a_copied_value_are_gone() {
    let before = a_choice_on_constants();
    scalar::check(&before).unwrap();
    let after = scalar::passes(&before);
    scalar::check(&after).unwrap();
    assert_eq!(scalar::run(&after), scalar::run(&before));
    assert_eq!(scalar::run(&after).stdout, b"A");
    let [main] = &after.functions[..] else {
        panic!()
    };
    // The choice, its arithmetic, the block it did not take, and the jumps
    // between the rest are one block that writes and returns.
    assert_eq!(main.blocks.len(), 1, "{after:?}");
    let ops: Vec<&Op> = main.blocks[0].instructions.iter().map(|i| &i.op).collect();
    assert!(
        ops.iter()
            .all(|op| matches!(op, Op::Constant(_) | Op::System { .. })),
        "{ops:?}"
    );
    assert_eq!(left_to_fold(&after), Vec::<String>::new());
    assert_eq!(left_to_remove(&after), Vec::<String>::new());
}

/// A loop that counts to three and writes `A` each time, which holds a
/// value that it gives to itself on every turn.
fn a_loop_with_a_value_it_carries() -> scalar::Program {
    let long = Type::Int(Bits::B64);
    let mut m = Builder::new();
    let (head, head_params) = m.block(&[long, long]);
    let (body, _) = m.block(&[]);
    let (done, _) = m.block(&[]);
    let zero = m.int(Bits::B64, 0);
    let one = m.int(Bits::B64, 1);
    let three = m.int(Bits::B64, 3);
    m.jump(head, vec![zero, one]);
    m.at(head);
    let (count, carried) = (head_params[0], head_params[1]);
    let finished = m.compare(scalar::Compare::Equal, count, three);
    m.switch(finished, vec![(body, Vec::new()), (done, Vec::new())]);
    m.at(body);
    let text = m.emit(Op::Constant(Constant::Data(0)), &[Type::Address])[0];
    let doubled = m.binary(scalar::Binary::Add, carried, carried);
    m.emit(
        Op::System {
            call: System::Write(Stream::Stdout),
            args: vec![text, doubled],
        },
        &[],
    );
    let next = m.binary(scalar::Binary::Add, count, one);
    m.jump(head, vec![next, carried]);
    m.at(done);
    m.ret(Vec::new());
    scalar::Program {
        data: vec![b"AA".to_vec()],
        functions: vec![m.finish(&[])],
        main: 0,
    }
}

fn a_value_a_loop_carries_unchanged_is_the_value_it_started_with() {
    let before = a_loop_with_a_value_it_carries();
    scalar::check(&before).unwrap();
    let after = scalar::passes(&before);
    scalar::check(&after).unwrap();
    assert_eq!(scalar::run(&after), scalar::run(&before));
    assert_eq!(scalar::run(&after).stdout, b"AAAAAA");
    assert_eq!(left_to_remove(&after), Vec::<String>::new(), "{after:?}");
    assert_eq!(left_to_fold(&after), Vec::<String>::new(), "{after:?}");
    let [main] = &after.functions[..] else {
        panic!()
    };
    // The loop keeps one parameter, the count.
    assert_eq!(main.blocks.iter().map(|b| b.params.len()).sum::<usize>(), 1);
}

/// A loop that counts to three and writes `AA` each time, which holds a
/// value that it gives to itself on every turn, through a block of its own.
fn a_loop_with_a_value_it_passes_on() -> scalar::Program {
    let long = Type::Int(Bits::B64);
    let mut m = Builder::new();
    let (head, head_params) = m.block(&[long, long]);
    let (through, through_params) = m.block(&[long]);
    let (done, _) = m.block(&[]);
    let zero = m.int(Bits::B64, 0);
    let one = m.int(Bits::B64, 1);
    let three = m.int(Bits::B64, 3);
    m.jump(head, vec![zero, one]);
    m.at(head);
    let (count, carried) = (head_params[0], head_params[1]);
    let finished = m.compare(scalar::Compare::Equal, count, three);
    m.switch(finished, vec![(through, vec![carried]), (done, Vec::new())]);
    m.at(through);
    let passed = through_params[0];
    let text = m.emit(Op::Constant(Constant::Data(0)), &[Type::Address])[0];
    let doubled = m.binary(scalar::Binary::Add, passed, passed);
    m.emit(
        Op::System {
            call: System::Write(Stream::Stdout),
            args: vec![text, doubled],
        },
        &[],
    );
    let next = m.binary(scalar::Binary::Add, count, one);
    m.jump(head, vec![next, passed]);
    m.at(done);
    m.ret(Vec::new());
    scalar::Program {
        data: vec![b"AA".to_vec()],
        functions: vec![m.finish(&[])],
        main: 0,
    }
}

fn a_value_a_loop_passes_through_another_block_unchanged_is_the_value_it_started_with() {
    let before = a_loop_with_a_value_it_passes_on();
    scalar::check(&before).unwrap();
    let after = scalar::passes(&before);
    scalar::check(&after).unwrap();
    assert_eq!(scalar::run(&after), scalar::run(&before));
    assert_eq!(scalar::run(&after).stdout, b"AAAAAA");
    assert_eq!(left_to_remove(&after), Vec::<String>::new(), "{after:?}");
    assert_eq!(left_to_fold(&after), Vec::<String>::new(), "{after:?}");
    let [main] = &after.functions[..] else {
        panic!()
    };
    // The loop keeps one parameter, the count.
    assert_eq!(main.blocks.iter().map(|b| b.params.len()).sum::<usize>(), 1);
}

/// A function that writes `A` once for each of the numbers from its
/// parameter down to 0, by a jump back to its first block, that is too large
/// to be inlined, and a `main` that calls it with 1 and with 2.
fn a_function_that_loops_in_its_first_block() -> scalar::Program {
    let long = Type::Int(Bits::B64);
    let mut f = Builder::new();
    let [n] = f.params(&[long])[..] else { panic!() };
    let (end, _) = f.block(&[]);
    let zero = f.int(Bits::B64, 0);
    let one = f.int(Bits::B64, 1);
    let text = f.emit(Op::Constant(Constant::Data(0)), &[Type::Address])[0];
    f.emit(
        Op::System {
            call: System::Write(Stream::Stdout),
            args: vec![text, one],
        },
        &[],
    );
    let finished = f.compare(scalar::Compare::Equal, n, zero);
    let next = f.binary(scalar::Binary::Sub, n, one);
    f.switch(finished, vec![(0, vec![next]), (end, Vec::new())]);
    f.at(end);
    f.ret(Vec::new());
    let looping = f.finish(&[]);
    let mut m = Builder::new();
    let one = m.int(Bits::B64, 1);
    let two = m.int(Bits::B64, 2);
    m.call(1, vec![one], &[]);
    m.call(1, vec![two], &[]);
    m.ret(Vec::new());
    scalar::Program {
        data: vec![b"A".to_vec()],
        functions: vec![m.finish(&[]), looping],
        main: 0,
    }
}

fn a_function_that_jumps_back_to_its_first_block_is_given_by_the_passes_as_it_was() {
    let before = a_function_that_loops_in_its_first_block();
    scalar::check(&before).unwrap();
    assert_eq!(scalar::run(&before).stdout, b"AAAAA");
    for after in after_each_pass(&before) {
        scalar::check(&after).unwrap();
        assert_eq!(scalar::run(&after), scalar::run(&before), "{after:?}");
    }
    let after = scalar::passes(&before);
    assert_eq!(left_to_fold(&after), Vec::<String>::new(), "{after:?}");
    assert_eq!(left_to_remove(&after), Vec::<String>::new(), "{after:?}");
    assert_eq!(scalar::passes(&after), after);
}

/// The blocks a function reaches from its first, when the block `without`,
/// if there is one, is taken out of it.
fn reached_without(f: &scalar::Function, without: Option<usize>) -> Vec<bool> {
    let mut seen = vec![false; f.blocks.len()];
    let mut work = vec![0];
    while let Some(b) = work.pop() {
        if seen[b] || Some(b) == without {
            continue;
        }
        seen[b] = true;
        work.extend(f.blocks[b].end.successors());
    }
    seen
}

/// Whether every path to the block `b` passes through the block `a`.
fn dominates(f: &scalar::Function, a: usize, b: usize) -> bool {
    a == b || !reached_without(f, Some(a))[b]
}

/// The pairs of instructions in the program of which the second does what
/// the first does, to the same values, and the first's value reaches it,
/// as a message for each. Only what reads no memory, writes none, and
/// calls nothing is looked at.
fn worked_out_twice(p: &scalar::Program) -> Vec<String> {
    let mut left = Vec::new();
    for (function, f) in p.functions.iter().enumerate() {
        let reached = reached_without(f, None);
        let mut same: HashMap<(Op, Type), Vec<(usize, usize)>> = HashMap::new();
        for (b, block) in f.blocks.iter().enumerate().filter(|(b, _)| reached[*b]) {
            for (i, instruction) in block.instructions.iter().enumerate() {
                let pure = matches!(
                    instruction.op,
                    Op::Constant(_)
                        | Op::Binary { .. }
                        | Op::Compare { .. }
                        | Op::Widen { .. }
                        | Op::Narrow(_)
                        | Op::Choose { .. }
                );
                if pure {
                    let ty = f.types[instruction.results[0].0];
                    same.entry((instruction.op.clone(), ty))
                        .or_default()
                        .push((b, i));
                }
            }
        }
        let mut groups: Vec<_> = same.into_iter().filter(|(_, at)| at.len() > 1).collect();
        groups.sort_by_key(|(_, at)| at[0]);
        for (key, at) in groups {
            for &(b, i) in &at {
                for &(c, j) in &at {
                    if (b, i) != (c, j) && (if b == c { i < j } else { dominates(f, b, c) }) {
                        left.push(format!(
                            "function {function}: {key:?} in block {b} and again in block {c}"
                        ));
                    }
                }
            }
        }
    }
    left
}

fn sharing_leaves_no_instruction_that_does_what_an_earlier_one_does() {
    for (_, s) in taken() {
        let after = scalar::passes(s);
        assert_eq!(worked_out_twice(&after), Vec::<String>::new(), "{after:?}");
    }
}

/// How many reads through an address the program holds.
fn reads(p: &scalar::Program) -> usize {
    let instructions = p.functions.iter().flat_map(|f| &f.blocks);
    let instructions = instructions.flat_map(|b| &b.instructions);
    instructions
        .filter(|i| matches!(i.op, Op::Load { .. }))
        .count()
}

fn sharing_never_replaces_a_read_through_an_address() {
    let mut some = 0;
    for (_, s) in taken() {
        let folded = scalar::fold(&scalar::inline(s));
        let shared = scalar::common(&folded);
        assert_eq!(reads(&shared), reads(&folded));
        some += reads(&shared);
    }
    assert!(some > 0, "no generated program reads through an address");
}

/// A program whose first block works out a sum and makes a constant, then
/// chooses between two blocks that each work out the sum and make the
/// constant again, around two reads of an address and a write, and a block
/// they both jump to that does it once more.
fn the_same_thing_in_blocks_that_follow() -> scalar::Program {
    let long = Type::Int(Bits::B64);
    let byte = Type::Int(Bits::B8);
    let mut m = Builder::new();
    let (left, _) = m.block(&[]);
    let (right, _) = m.block(&[]);
    let (join, _) = m.block(&[]);
    let eight = m.int(Bits::B64, 8);
    let address = m.allocate(eight);
    let x = m.load(long, address);
    let flag = m.load(byte, address);
    let one = m.int(Bits::B64, 1);
    let sum = m.binary(scalar::Binary::Add, x, one);
    m.store(address, sum);
    m.switch(flag, vec![(left, Vec::new()), (right, Vec::new())]);
    for (arm, text) in [(left, 0), (right, 1)] {
        m.at(arm);
        let one = m.int(Bits::B64, 1);
        let sum = m.binary(scalar::Binary::Add, x, one);
        let first = m.load(long, address);
        m.store(address, sum);
        let second = m.load(long, address);
        let both = m.binary(scalar::Binary::Add, first, second);
        m.store(address, both);
        let text = m.emit(Op::Constant(Constant::Data(text)), &[Type::Address])[0];
        m.emit(
            Op::System {
                call: System::Write(Stream::Stdout),
                args: vec![text, one],
            },
            &[],
        );
        m.jump(join, Vec::new());
    }
    m.at(join);
    let one = m.int(Bits::B64, 1);
    let sum = m.binary(scalar::Binary::Add, x, one);
    m.store(address, sum);
    m.ret(Vec::new());
    scalar::Program {
        data: vec![b"A".to_vec(), b"B".to_vec()],
        functions: vec![m.finish(&[])],
        main: 0,
    }
}

fn what_is_worked_out_in_a_block_that_dominates_another_is_not_worked_out_again() {
    let before = the_same_thing_in_blocks_that_follow();
    scalar::check(&before).unwrap();
    assert_ne!(worked_out_twice(&before), Vec::<String>::new());
    let after = scalar::passes(&before);
    scalar::check(&after).unwrap();
    for limit in [0, 8, usize::MAX] {
        assert_eq!(
            scalar::run_with_limit(&after, limit),
            scalar::run_with_limit(&before, limit)
        );
    }
    assert_eq!(worked_out_twice(&after), Vec::<String>::new(), "{after:?}");
    let [main] = &after.functions[..] else {
        panic!()
    };
    let ops = main.blocks.iter().flat_map(|b| &b.instructions);
    let ops: Vec<&Op> = ops.map(|i| &i.op).collect();
    let count = |is: &dyn Fn(&Op) -> bool| ops.iter().filter(|op| is(op)).count();
    // The constant is made once, and the sum is worked out once, however
    // many blocks ask for it.
    let ones = count(&|op| matches!(op, Op::Constant(Constant::Int(1))));
    assert_eq!(ones, 1, "{after:?}");
    let sums = count(&|op| matches!(op, Op::Binary { .. }));
    assert_eq!(sums, 3, "{after:?}");
    // Every read stays, as the form does not say what a write changes.
    assert_eq!(reads(&after), reads(&before));
}
