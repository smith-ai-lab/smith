//! Comparing two whole numbers, or two `bool`s, with `<`, `<=`, `>`, `>=`,
//! `==`, or `!=` is one comparison in the scalar form and no call, and a
//! branch on it is one compare and one jump on every target, and every
//! operator answers what it must at the edges of every type.
//!
//! The structure is counted against the same program in which the function
//! under test answers `true`, so what the printing of a program costs is in
//! neither count. The answers are checked against Rust's, on values that the
//! compiler cannot know because they come out of a list.

use smith::scalar::{self, End, Op};
use smith::target::Target;
use smith::{compile, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::machine_code::{self, Kind};
use crate::numbers::{INTS, value};
use crate::targets::{runnable, runs_as};

pub const ONCE: &[Test] = &[
    (
        "a_comparison_of_two_i32_is_one_comparison_and_no_call",
        || adds_one_comparison(0, |_| None),
    ),
    (
        "a_comparison_of_two_i32_through_ord_or_eq_is_one_comparison_and_no_call",
        || adds_one_comparison(0, bound),
    ),
    (
        "the_evaluator_answers_every_comparison_of_i32_as_rust_does",
        || answers_are_what_rust_says(0),
    ),
    (
        "a_comparison_of_two_u8_is_one_comparison_and_no_call",
        || adds_one_comparison(1, |_| None),
    ),
    (
        "a_comparison_of_two_u8_through_ord_or_eq_is_one_comparison_and_no_call",
        || adds_one_comparison(1, bound),
    ),
    (
        "the_evaluator_answers_every_comparison_of_u8_as_rust_does",
        || answers_are_what_rust_says(1),
    ),
    (
        "a_comparison_of_two_i8_is_one_comparison_and_no_call",
        || adds_one_comparison(2, |_| None),
    ),
    (
        "a_comparison_of_two_i8_through_ord_or_eq_is_one_comparison_and_no_call",
        || adds_one_comparison(2, bound),
    ),
    (
        "the_evaluator_answers_every_comparison_of_i8_as_rust_does",
        || answers_are_what_rust_says(2),
    ),
    (
        "a_comparison_of_two_u16_is_one_comparison_and_no_call",
        || adds_one_comparison(3, |_| None),
    ),
    (
        "a_comparison_of_two_u16_through_ord_or_eq_is_one_comparison_and_no_call",
        || adds_one_comparison(3, bound),
    ),
    (
        "the_evaluator_answers_every_comparison_of_u16_as_rust_does",
        || answers_are_what_rust_says(3),
    ),
    (
        "a_comparison_of_two_i16_is_one_comparison_and_no_call",
        || adds_one_comparison(4, |_| None),
    ),
    (
        "a_comparison_of_two_i16_through_ord_or_eq_is_one_comparison_and_no_call",
        || adds_one_comparison(4, bound),
    ),
    (
        "the_evaluator_answers_every_comparison_of_i16_as_rust_does",
        || answers_are_what_rust_says(4),
    ),
    (
        "a_comparison_of_two_u32_is_one_comparison_and_no_call",
        || adds_one_comparison(5, |_| None),
    ),
    (
        "a_comparison_of_two_u32_through_ord_or_eq_is_one_comparison_and_no_call",
        || adds_one_comparison(5, bound),
    ),
    (
        "the_evaluator_answers_every_comparison_of_u32_as_rust_does",
        || answers_are_what_rust_says(5),
    ),
    (
        "a_comparison_of_two_i64_is_one_comparison_and_no_call",
        || adds_one_comparison(6, |_| None),
    ),
    (
        "a_comparison_of_two_i64_through_ord_or_eq_is_one_comparison_and_no_call",
        || adds_one_comparison(6, bound),
    ),
    (
        "the_evaluator_answers_every_comparison_of_i64_as_rust_does",
        || answers_are_what_rust_says(6),
    ),
    (
        "a_comparison_of_two_u64_is_one_comparison_and_no_call",
        || adds_one_comparison(7, |_| None),
    ),
    (
        "a_comparison_of_two_u64_through_ord_or_eq_is_one_comparison_and_no_call",
        || adds_one_comparison(7, bound),
    ),
    (
        "the_evaluator_answers_every_comparison_of_u64_as_rust_does",
        || answers_are_what_rust_says(7),
    ),
    (
        "a_comparison_of_two_bool_is_one_comparison_and_no_call",
        || adds_one_comparison(8, |_| None),
    ),
    (
        "a_comparison_of_two_bool_through_ord_or_eq_is_one_comparison_and_no_call",
        || adds_one_comparison(8, bound),
    ),
    (
        "the_evaluator_answers_every_comparison_of_bool_as_rust_does",
        || answers_are_what_rust_says(8),
    ),
];

pub const MACHINE: &[Test] = &[
    (
        "every_executable_answers_every_comparison_of_i32_as_rust_does",
        || answers_on_every_executable(0),
    ),
    (
        "a_branch_on_a_comparison_of_two_i32_is_one_compare_and_one_jump_on_every_target",
        || a_branch_is_one_compare_and_one_jump(0),
    ),
    (
        "every_executable_answers_every_comparison_of_u8_as_rust_does",
        || answers_on_every_executable(1),
    ),
    (
        "a_branch_on_a_comparison_of_two_u8_is_one_compare_and_one_jump_on_every_target",
        || a_branch_is_one_compare_and_one_jump(1),
    ),
    (
        "every_executable_answers_every_comparison_of_i8_as_rust_does",
        || answers_on_every_executable(2),
    ),
    (
        "a_branch_on_a_comparison_of_two_i8_is_one_compare_and_one_jump_on_every_target",
        || a_branch_is_one_compare_and_one_jump(2),
    ),
    (
        "every_executable_answers_every_comparison_of_u16_as_rust_does",
        || answers_on_every_executable(3),
    ),
    (
        "a_branch_on_a_comparison_of_two_u16_is_one_compare_and_one_jump_on_every_target",
        || a_branch_is_one_compare_and_one_jump(3),
    ),
    (
        "every_executable_answers_every_comparison_of_i16_as_rust_does",
        || answers_on_every_executable(4),
    ),
    (
        "a_branch_on_a_comparison_of_two_i16_is_one_compare_and_one_jump_on_every_target",
        || a_branch_is_one_compare_and_one_jump(4),
    ),
    (
        "every_executable_answers_every_comparison_of_u32_as_rust_does",
        || answers_on_every_executable(5),
    ),
    (
        "a_branch_on_a_comparison_of_two_u32_is_one_compare_and_one_jump_on_every_target",
        || a_branch_is_one_compare_and_one_jump(5),
    ),
    (
        "every_executable_answers_every_comparison_of_i64_as_rust_does",
        || answers_on_every_executable(6),
    ),
    (
        "a_branch_on_a_comparison_of_two_i64_is_one_compare_and_one_jump_on_every_target",
        || a_branch_is_one_compare_and_one_jump(6),
    ),
    (
        "every_executable_answers_every_comparison_of_u64_as_rust_does",
        || answers_on_every_executable(7),
    ),
    (
        "a_branch_on_a_comparison_of_two_u64_is_one_compare_and_one_jump_on_every_target",
        || a_branch_is_one_compare_and_one_jump(7),
    ),
    (
        "every_executable_answers_every_comparison_of_bool_as_rust_does",
        || answers_on_every_executable(8),
    ),
    (
        "a_branch_on_a_comparison_of_two_bool_is_one_compare_and_one_jump_on_every_target",
        || a_branch_is_one_compare_and_one_jump(8),
    ),
];

/// `==` and `!=` are of `Eq`, the others of `Ord`.
fn bound(k: usize) -> Option<&'static str> {
    Some(if k < 4 { "Ord" } else { "Eq" })
}

/// The operators, and how Rust answers each.
/// An operator and what Rust answers for it.
type Operator = (&'static str, fn(i128, i128) -> bool);

const OPS: [Operator; 6] = [
    ("<", |a, b| a < b),
    ("<=", |a, b| a <= b),
    (">", |a, b| a > b),
    (">=", |a, b| a >= b),
    ("==", |a, b| a == b),
    ("!=", |a, b| a != b),
];

/// The types the tests go over: every whole number type, and `bool`.
fn types() -> Vec<(&'static str, i128, i128, bool)> {
    let mut all: Vec<_> = INTS
        .iter()
        .map(|t| (t.name, t.min(), t.max(), t.signed))
        .collect();
    all.push(("bool", 0, 1, false));
    all
}

/// How a value of the type is written.
fn literal(name: &str, n: i128) -> String {
    match (name, n) {
        ("bool", 0) => "false".to_string(),
        ("bool", _) => "true".to_string(),
        _ => n.to_string(),
    }
}

/// A value of the type that the compiler cannot know, made from the size of
/// the arguments, which is not known until the program runs: the `n` that
/// `COUNT` binds, which is their size as a `u64`.
fn unknown(name: &str, plus: u64) -> String {
    let size = format!("wrapping_add(n, {plus})");
    match name {
        "bool" => format!("{size} == 1"),
        "u64" => size,
        _ => format!("wrapping_narrow({size})"),
    }
}

/// The line that binds the size of the arguments to a `u64`, which wraps.
const COUNT: &str = "  n: u64 = convert(args.size)\n";

/// How many comparisons and how many calls a program has after the passes.
fn counted(source: &str) -> (usize, usize) {
    let program = scalar::passes(&lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}")));
    let ops = program
        .functions
        .iter()
        .flat_map(|f| &f.blocks)
        .flat_map(|b| &b.instructions)
        .map(|i| &i.op);
    let (mut compares, mut calls) = (0, 0);
    for op in ops {
        match op {
            Op::Compare { .. } => compares += 1,
            Op::Call { .. } => calls += 1,
            _ => {}
        }
    }
    (compares, calls)
}

/// A program of six functions that each give a comparison of two values of
/// the type that the program cannot know, of which the one at `at` is the
/// comparison and the others give `true`, which the program uses by
/// printing and by an `if`.
fn program_with(name: &str, at: Option<usize>, bound: Option<&str>) -> String {
    let mut source = String::new();
    for (k, (op, _)) in OPS.iter().enumerate() {
        let body = if at == Some(k) {
            format!("a {op} b")
        } else {
            "true".to_string()
        };
        match bound {
            None => source += &format!("fn f{k}(a: {name}, b: {name}) -> bool {{ {body} }}\n"),
            Some(bound) => {
                source += &format!("fn f{k}[T: {bound}](a: T, b: T) -> bool {{ {body} }}\n")
            }
        }
    }
    source += "fn main(args: List[str]) ! console, alloc {\n";
    source += COUNT;
    source += &format!("  x: {name} = {}\n", unknown(name, 0));
    source += &format!("  y: {name} = {}\n", unknown(name, 1));
    for k in 0..OPS.len() {
        source += &format!("  print(\"{{f{k}(x, y)}}\")\n");
        source += &format!("  if f{k}(y, x) {{ print(\"yes\") }} else {{ print(\"no\") }}\n");
    }
    source += "}\n";
    source
}

/// For each operator: the comparison adds exactly one comparison for each of
/// its two uses and no call to what the program has when the function gives
/// `true`.
fn adds_one_comparison(index: usize, bound: impl Fn(usize) -> Option<&'static str>) {
    let (name, ..) = types()[index];
    let base = counted(&program_with(name, None, bound(0)));
    // The two `bool`s are made by a comparison each.
    let made = if name == "bool" { 2 } else { 0 };
    for (k, (op, _)) in OPS.iter().enumerate() {
        let source = program_with(name, Some(k), bound(k));
        let (compares, calls) = counted(&source);
        assert_eq!(
            (compares, calls),
            (base.0 + 2 + made, base.1),
            "`{}` of `{name}` is not one comparison for each of its two uses and no call in\n{source}",
            op
        );
    }
}

/// The values a type is tried with: its least, its greatest, and those next
/// to them, -1, 0, and 1 where the type has them, and a few the seed picks.
fn tried(name: &str, min: i128, max: i128, signed: bool, g: &mut Gen) -> Vec<i128> {
    let mut values = vec![min, max];
    if name != "bool" {
        values.extend([min + 1, min + 2, 0, 1, 2, max - 1, max - 2]);
        if signed {
            values.push(-1);
        }
        if let Some(int) = INTS.iter().find(|t| t.name == name) {
            for _ in 0..4 {
                values.push(value(g, *int));
            }
        }
    }
    values.retain(|v| (min..=max).contains(v));
    values.sort();
    values.dedup();
    values
}

/// A program that prints, for every two of the values in a list the
/// compiler cannot see through, the answer of each operator as a value, then
/// as the condition of an `if`, as a number whose bits say which held, and
/// how many times a `loop` over the range from the first to the second, and a
/// `loop` that ends when `>=` says so, runs, for the pairs whose range is
/// short. It also gives what Rust says each prints.
fn answers(name: &str, min: i128, max: i128, signed: bool, g: &mut Gen) -> (String, String) {
    let values = tried(name, min, max, signed, g);
    let mut source = format!("fn code(a: {name}, b: {name}) -> u64 {{\n  var r: u64 = 0\n");
    for (k, (op, _)) in OPS.iter().enumerate() {
        source += &format!("  if a {op} b {{ r = wrapping_add(r, {}) }}\n", 1 << k);
    }
    source += "  r\n}\n";
    if name != "bool" {
        source += &format!(
            "fn count(a: {name}, b: {name}) -> u64 ! diverge {{\n  var n: u64 = 0\n  loop k in a..b {{ n = wrapping_add(n, 1) }}\n  var i: {name} = a\n  loop {{\n    if i >= b {{ break }}\n    i = wrapping_add(i, 1)\n    n = wrapping_add(n, 1)\n  }}\n  n\n}}\n"
        );
    }
    let list: Vec<String> = values.iter().map(|&v| literal(name, v)).collect();
    source += &format!(
        "fn main() ! console, alloc, diverge {{\n  xs: List[{name}] = [{}]\n  loop i in 0..xs.size {{\n    loop j in 0..xs.size {{\n      a = xs[i]\n      b = xs[j]\n      print(\"{{a < b}} {{a <= b}} {{a > b}} {{a >= b}} {{a == b}} {{a != b}} {{code(a, b)}}\")\n    }}\n  }}\n",
        list.join(", ")
    );
    let mut said = String::new();
    for &a in &values {
        for &b in &values {
            let held: Vec<bool> = OPS.iter().map(|(_, op)| op(a, b)).collect();
            let words: Vec<String> = held.iter().map(|h| h.to_string()).collect();
            let code: u32 = held.iter().enumerate().map(|(k, &h)| (h as u32) << k).sum();
            said += &format!("{} {code}\n", words.join(" "));
        }
    }
    if name != "bool" {
        for (i, &a) in values.iter().enumerate() {
            for (j, &b) in values.iter().enumerate() {
                if b <= a || b - a <= 4 {
                    source += &format!("  print(\"{{count(xs[{i}], xs[{j}])}}\")\n");
                    said += &format!("{}\n", if b > a { 2 * (b - a) } else { 0 });
                }
            }
        }
    }
    source += "}\n";
    (source, said)
}

fn answers_are_what_rust_says(index: usize) {
    let (name, min, max, signed) = types()[index];
    let (source, said) =
        generate::sample(index as u64, 0, |g| answers(name, min, max, signed, g));
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let outcome = scalar::run(&scalar::passes(&program));
    assert_eq!(
        String::from_utf8_lossy(&outcome.stdout),
        said,
        "for {source}"
    );
    assert_eq!(outcome.status, 0);
}

fn answers_on_every_executable(index: usize) {
    let (name, min, max, signed) = types()[index];
    let (source, said) =
        generate::sample(index as u64, 0, |g| answers(name, min, max, signed, g));
    let outcome = scalar::Outcome {
        stdout: said.into_bytes(),
        status: 0,
    };
    for target in runnable() {
        runs_as(source.as_bytes(), target, &outcome);
    }
}

/// How many comparisons the program has, and how many only the choice that
/// ends their block reads.
fn comparisons(program: &scalar::Program) -> (usize, usize) {
    let (mut all, mut branched) = (0, 0);
    for f in &program.functions {
        let mut reads = vec![0usize; f.types.len()];
        for block in &f.blocks {
            let operands = block.instructions.iter().flat_map(|i| i.op.operands());
            for v in operands.chain(block.end.operands()) {
                reads[v.0] += 1;
            }
        }
        for block in &f.blocks {
            all += (block.instructions.iter())
                .filter(|i| matches!(i.op, Op::Compare { .. }))
                .count();
            if let (End::Switch { on, jumps }, Some(last)) = (&block.end, block.instructions.last())
                && jumps.len() == 2
                && reads[on.0] == 1
                && last.results == [*on]
                && matches!(last.op, Op::Compare { .. })
            {
                branched += 1;
            }
        }
    }
    (all, branched)
}

/// A program that branches on each operator, each on two values of its own
/// that it cannot know, so that no two comparisons are one. With `ops` false
/// each condition is `true` instead, and with `equal` each operator is `==`.
fn branching(name: &str, ops: bool, equal: bool) -> String {
    let mut source = format!("fn main(args: List[str]) ! console, alloc {{\n{COUNT}");
    for (k, (op, _)) in OPS.iter().enumerate() {
        let condition = if ops {
            source += &format!("  x{k}: {name} = {}\n", unknown(name, 2 * k as u64));
            source += &format!("  y{k}: {name} = {}\n", unknown(name, 2 * k as u64 + 1));
            format!("x{k} {} y{k}", if equal { "==" } else { op })
        } else {
            "true".to_string()
        };
        source += &format!("  if {condition} {{ print(\"yes\") }} else {{ print(\"no\") }}\n");
    }
    source + "}\n"
}

/// For the type, `if a < b { x } else { y }` of each operator is one
/// comparison and one choice between blocks, and on every target that is an
/// ELF file one compare and one conditional jump: no comparison is made a
/// value in a register that is then tested.
fn a_branch_is_one_compare_and_one_jump(index: usize) {
    let mut read = 0;
    for (name, _, _, signed) in [types()[index]] {
        let source = branching(name, true, false);
        let base = branching(name, false, false);
        let equal = branching(name, true, true);
        let program = scalar::passes(&lower(source.as_bytes()).unwrap());
        let (all, branched) = comparisons(&program);
        let (base_all, base_branched) =
            comparisons(&scalar::passes(&lower(base.as_bytes()).unwrap()));
        // Each of the six branches adds a comparison that only its choice
        // reads, and nothing else that compares, but for the comparison
        // that makes each of the twelve values of a `bool`.
        let made = if name == "bool" { 12 } else { 0 };
        assert_eq!(
            (all - base_all, branched - base_branched),
            (6 + made, 6),
            "`{name}`: the comparisons the six branches add, and those only a choice reads, in\n{source}"
        );
        for target in Target::ALL {
            let bytes = compile(source.as_bytes(), *target).unwrap();
            let Some((_, found)) = machine_code::instructions(&bytes) else {
                continue;
            };
            read += 1;
            let values = |found: &[machine_code::Instruction]| {
                found.iter().filter(|i| i.kind == Kind::SetCondition).count()
            };
            let jumps = |found: &[machine_code::Instruction]| {
                (found.iter()).filter(|i| matches!(i.kind, Kind::Branch(_))).count()
            };
            // Every operator is what `==` is, which is the one compare and
            // the one jump that a branch on a comparison always was.
            let (_, same) = machine_code::instructions(&compile(equal.as_bytes(), *target).unwrap())
                .unwrap();
            assert_eq!(values(&found), values(&same), "a value is made on {target:?}: {source}");
            assert_eq!(jumps(&found), jumps(&same), "jumps differ on {target:?}: {source}");
            // The one thing more that a branch may hold is the sign
            // extension of the two operands of each of the four operators
            // that read a signed number narrower than a register.
            let more = if signed && name != "i64" { 4 * 2 } else { 0 };
            assert!(
                (same.len()..=same.len() + more).contains(&found.len()),
                "{} instructions, and {} for `==`, on {target:?}: {source}",
                found.len(),
                same.len()
            );
        }
    }
    assert!(read > 0, "no target is an ELF file");
}

