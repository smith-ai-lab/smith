//! What `saturating_add`, `saturating_sub`, `checked_add`, and `checked_sub`
//! of an unsigned whole number or a `Nat` cost, and that they give what they
//! gave: the cost is an add or a subtraction and a choice, with no call and no
//! jump for a `saturating_` function, and a jump for a `checked_` function that
//! is matched at once. Every function answers what Rust's arithmetic on 128
//! bits says, at the edges of every type, as a value and matched at once.
//!
//! What a function costs is counted in the machine code of each kind of chip,
//! beside the same program with the function a `wrapping_` one, which is
//! the add or the subtraction alone, and the values are taken from a list, so
//! that the compiler cannot know them.

use smith::scalar;
use smith::target::Target;
use smith::lower;

use crate::Test;
use crate::aarch64;
use crate::generate::{self, Gen};
use crate::machine_code::{self, Kind};
use crate::numbers::{INTS, Int, arithmetic, value};
use crate::targets::{runnable, scalar_runs_as};

pub const ONCE: &[Test] = &[
    (
        "a_saturating_add_or_sub_of_an_unsigned_type_is_an_add_or_a_sub_and_a_choice_with_no_call_and_no_jump",
        a_saturating_add_or_sub_of_an_unsigned_type_is_an_add_or_a_sub_and_a_choice_with_no_call_and_no_jump,
    ),
    (
        "a_checked_add_or_sub_of_an_unsigned_type_that_is_matched_at_once_is_what_an_if_on_the_wrapping_one_is",
        a_checked_add_or_sub_of_an_unsigned_type_that_is_matched_at_once_is_what_an_if_on_the_wrapping_one_is,
    ),
    (
        "the_saturating_and_checked_functions_answer_at_the_edges_of_every_type_in_the_evaluator_and_on_arm64",
        the_saturating_and_checked_functions_answer_at_the_edges_of_every_type_in_the_evaluator_and_on_arm64,
    ),
];

pub const MACHINE: &[Test] = &[(
    "the_saturating_and_checked_functions_answer_at_the_edges_of_every_type_in_every_executable",
    the_saturating_and_checked_functions_answer_at_the_edges_of_every_type_in_every_executable,
)];

/// The types of the unsigned whole numbers, and `Nat`, which is lowered to
/// the `u64` functions.
const UNSIGNED: [&str; 5] = ["u8", "u16", "u32", "u64", "Nat"];

/// What a use of `saturating_add` or `saturating_sub` may cost over the
/// `wrapping_` function of the same operation: a comparison, the end that is
/// chosen, and the choice.
const EXTRA: usize = 3;

/// The type of the values that the program makes from a list, and what the
/// wrapping function that stands beside a `Nat` is, which has none.
fn wrapping_type(ty: &str) -> &str {
    if ty == "Nat" { "u64" } else { ty }
}

/// A program that prints what `body` writes of two values of `ty` that the
/// compiler cannot know.
fn with_values(ty: &str, body: &str) -> String {
    format!("fn main() ! console, alloc {{\n  xs: List[{ty}] = [3, 4]\n  x = xs[0]\n  y = xs[1]\n{body}}}\n")
}

/// For each target whose executable the tests read, how many instructions it
/// holds, how many are calls, and how many jump.
fn counted(source: &str) -> Vec<(Target, (usize, usize, usize))> {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
    let program = scalar::passes(&program);
    Target::ALL
        .iter()
        .filter_map(|&target| {
            let (_, found) = machine_code::instructions(&target.emit(&program))?;
            let calls = found.iter().filter(|i| i.kind == Kind::Call).count();
            let jumps = (found.iter())
                .filter(|i| matches!(i.kind, Kind::Jump(_) | Kind::Branch(_)))
                .count();
            Some((target, (found.len(), calls, jumps)))
        })
        .collect()
}

/// For each type and each operation, on every chip, with the function used
/// twice so that it is not the one call that is replaced for being the only
/// one: the function has no call and no jump that the program with the
/// `wrapping_` function has not, and costs no more than `EXTRA` instructions
/// beyond it for each use.
fn a_saturating_add_or_sub_of_an_unsigned_type_is_an_add_or_a_sub_and_a_choice_with_no_call_and_no_jump() {
    let mut read = 0;
    for ty in UNSIGNED {
        for op in ["add", "sub"] {
            let program = |f: String, ty: &str| {
                with_values(ty, &format!("  print(\"{{{f}(x, y)}}\")\n  print(\"{{{f}(y, x)}}\")\n"))
            };
            let saturating = program(format!("saturating_{op}"), ty);
            let wrapping = program(format!("wrapping_{op}"), wrapping_type(ty));
            let (saturating, wrapping) = (counted(&saturating), counted(&wrapping));
            for ((target, s), (_, w)) in saturating.iter().zip(&wrapping) {
                read += 1;
                assert_eq!(
                    (s.1, s.2),
                    (w.1, w.2),
                    "`saturating_{op}` of `{ty}` makes a call or a jump on {target:?}"
                );
                assert!(
                    s.0 <= w.0 + 2 * EXTRA,
                    "`saturating_{op}` of `{ty}` is {} instructions, and `wrapping_{op}` is {}, on {target:?}",
                    s.0,
                    w.0
                );
            }
        }
    }
    assert!(read > 0, "no target is an ELF file");
}

/// A `checked_add` or `checked_sub` that is matched at once is the add or the
/// subtraction and the jump that an `if` on the same comparison is: the same
/// instructions, on every chip.
fn a_checked_add_or_sub_of_an_unsigned_type_that_is_matched_at_once_is_what_an_if_on_the_wrapping_one_is() {
    let mut read = 0;
    for ty in UNSIGNED {
        for op in ["add", "sub"] {
            let matched = with_values(
                ty,
                &format!(
                    "  match checked_{op}(x, y) {{\n    .some(v) {{ print(\"{{v}}\") }}\n    .none {{ print(\"none\") }}\n  }}\n"
                ),
            );
            let w = wrapping_type(ty);
            let written = with_values(
                w,
                match op {
                    "add" => "  r = wrapping_add(x, y)\n  if r < x { print(\"none\") } else { print(\"{r}\") }\n",
                    _ => "  r = wrapping_sub(x, y)\n  if x < y { print(\"none\") } else { print(\"{r}\") }\n",
                },
            );
            for ((target, m), (_, h)) in counted(&matched).iter().zip(&counted(&written)) {
                read += 1;
                assert_eq!(
                    m, h,
                    "`checked_{op}` of `{ty}` matched at once is not what an `if` is on {target:?}"
                );
            }
        }
    }
    assert!(read > 0, "no target is an ELF file");
}

/// The six functions, as they are written.
const FUNCTIONS: [(&str, &str, &str); 6] = [
    ("saturating", "add", "saturating_add"),
    ("saturating", "sub", "saturating_sub"),
    ("saturating", "mul", "saturating_mul"),
    ("checked", "add", "checked_add"),
    ("checked", "sub", "checked_sub"),
    ("checked", "mul", "checked_mul"),
];

/// The values a type is tried with: its least and greatest, those next to
/// them, 0, 1, and -1 where the type has them, and a few the seed picks.
fn tried(ty: Int, g: &mut Gen) -> Vec<i128> {
    let mut values = vec![ty.min(), ty.max(), ty.min() + 1, ty.max() - 1, 0, 1, 2];
    if ty.signed {
        values.push(-1);
    }
    for _ in 0..3 {
        values.push(value(g, ty));
    }
    values.retain(|v| (ty.min()..=ty.max()).contains(v));
    values.sort();
    values.dedup();
    values
}

/// A program that prints, for every two of the values, what each function
/// gives as a value, and what a `match` on each `checked_` function gives, and
/// what it must print.
fn answers(name: &str, ty: Int, g: &mut Gen) -> (String, String) {
    let values = tried(ty, g);
    let list: Vec<String> = values.iter().map(|v| v.to_string()).collect();
    let mut source = format!(
        "fn main() ! console, alloc, diverge {{\n  xs: List[{name}] = [{}]\n  loop i in 0..xs.size {{\n    loop j in 0..xs.size {{\n      x = xs[i]\n      y = xs[j]\n",
        list.join(", ")
    );
    for (_, _, f) in FUNCTIONS {
        source += &format!("      print(\"{{{f}(x, y)}}\")\n");
    }
    for (kind, _, f) in FUNCTIONS {
        if kind == "checked" {
            source += &format!(
                "      match {f}(x, y) {{\n        .some(v) {{ print(\"{{v}}\") }}\n        .none {{ print(\"none\") }}\n      }}\n"
            );
        }
    }
    source += "    }\n  }\n}\n";
    let mut said = String::new();
    for &a in &values {
        for &b in &values {
            for (kind, op, _) in FUNCTIONS {
                said += &arithmetic(kind, op, ty, a, b);
                said += "\n";
            }
            for (kind, op, _) in FUNCTIONS {
                if kind == "checked" {
                    let r = arithmetic(kind, op, ty, a, b);
                    said += &match r.strip_prefix(".some(") {
                        Some(n) => n.trim_end_matches(')').to_string(),
                        None => "none".to_string(),
                    };
                    said += "\n";
                }
            }
        }
    }
    (source, said)
}

/// Every type, `Nat` as a `u64` whose name is `Nat`, with the program that
/// tries it and what it prints.
fn programs(g: &mut Gen) -> Vec<(String, String, String)> {
    let mut all: Vec<(String, String, String)> = INTS
        .iter()
        .map(|ty| {
            let (source, said) = answers(ty.name, *ty, g);
            (ty.name.to_string(), source, said)
        })
        .collect();
    let u64 = *INTS.iter().find(|t| t.name == "u64").unwrap();
    let (source, said) = answers("Nat", u64, g);
    all.push(("Nat".to_string(), source, said));
    all
}

/// Every function answers what Rust says at the edges, in the evaluator and in
/// each executable that the machine of the tests runs for arm64.
fn the_saturating_and_checked_functions_answer_at_the_edges_of_every_type_in_the_evaluator_and_on_arm64() {
    generate::check(4401, 1, programs, |all| {
        for (name, source, said) in all {
            let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
            let out = scalar::run(&program);
            let printed = String::from_utf8(out.stdout).expect("text");
            assert_eq!(&printed, said, "the evaluator, for `{name}`:\n{source}");
            let built = scalar::passes(&program);
            let mut ran_arm64 = false;
            for target in Target::ALL {
                let bytes = target.emit(&built);
                if aarch64::is_executable(&bytes) {
                    ran_arm64 = true;
                    let printed = String::from_utf8(aarch64::run(&bytes).stdout).expect("text");
                    assert_eq!(&printed, said, "arm64 {target:?}, for `{name}`:\n{source}");
                }
            }
            assert!(ran_arm64, "an arm64 executable");
        }
    });
}

/// The same, in the executables that this machine runs.
fn the_saturating_and_checked_functions_answer_at_the_edges_of_every_type_in_every_executable() {
    let targets = runnable();
    generate::check(4402, 1, programs, |all| {
        for (name, source, said) in all {
            let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
            let out = scalar::run(&program);
            assert_eq!(&String::from_utf8(out.stdout.clone()).expect("text"), said, "`{name}`");
            for &target in &targets {
                scalar_runs_as(&scalar::passes(&program), target, &out);
            }
        }
    });
}
