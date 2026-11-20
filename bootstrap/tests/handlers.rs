//! An effect handled by calling its name in a function that is given a body,
//! installed with `with`: the code under the handler does not carry the effect,
//! each call of an operation runs the function the handler gave for it, and
//! the executable has no trace of the handler, whatever the operations take,
//! give, and leave to their defaults. The expectation of each program is what
//! the generator that wrote it chose the functions to give.

use smith::diag::Pos;
use smith::scalar::{self, Outcome};
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::targets;

pub const ONCE: &[Test] = &[
    (
        "a_handler_that_gives_a_value_answers_each_call_of_its_operation_and_the_code_under_it_carries_no_effect",
        a_handler_that_gives_a_value_answers_each_call_of_its_operation_and_the_code_under_it_carries_no_effect,
    ),
    (
        "a_body_given_to_a_handler_carries_its_effect_and_every_other_effect_is_required_where_it_is_written",
        a_body_given_to_a_handler_carries_its_effect_and_every_other_effect_is_required_where_it_is_written,
    ),
    (
        "a_call_of_an_operation_is_the_function_that_answers_it_lowered_in_place_and_leaves_no_call_behind",
        a_call_of_an_operation_is_the_function_that_answers_it_lowered_in_place_and_leaves_no_call_behind,
    ),
];

/// The executables of the programs the handlers make are checked against the
/// evaluator, on the targets this machine can run.
pub const MACHINE: &[Test] = &[(
    "every_executable_of_a_program_with_a_handler_prints_what_the_evaluator_says",
    every_executable_of_a_program_with_a_handler_prints_what_the_evaluator_says,
)];

/// What an operation's function gives.
#[derive(Clone, Debug)]
enum Gives {
    /// The same number whatever it is asked.
    Constant(i32),
    /// The argument in this place.
    Argument(usize),
}

/// An operation: for each parameter, the number it takes when a call leaves
/// it out, if it has a default, and what the handler gives for it.
#[derive(Clone, Debug)]
struct Operation {
    params: Vec<Option<i32>>,
    gives: Gives,
}

/// A call of operation `op`: the argument given for each parameter, or none
/// for a parameter with a default that the call leaves out.
#[derive(Clone, Debug)]
struct Call {
    op: usize,
    args: Vec<Option<i32>>,
}

/// A program: the operations of an effect, the calls made under a handler of
/// it, and whether the handler is written as a function that the calls are
/// passed to with `with`.
#[derive(Clone, Debug)]
struct Program {
    operations: Vec<Operation>,
    calls: Vec<Call>,
}

fn number(g: &mut Gen) -> i32 {
    g.below(2000) as i32 - 1000
}

fn program(g: &mut Gen) -> Program {
    let operations: Vec<Operation> = (0..g.below(3) + 1)
        .map(|_| {
            let count = g.below(4);
            let defaults = g.below(count + 1);
            let params: Vec<Option<i32>> = (0..count)
                .map(|i| (i >= count - defaults).then(|| number(g)))
                .collect();
            let gives = match g.below(2) {
                1 if count > 0 => Gives::Argument(g.below(count)),
                _ => Gives::Constant(number(g)),
            };
            Operation { params, gives }
        })
        .collect();
    let calls = (0..g.below(4) + 1)
        .map(|_| {
            let op = g.below(operations.len());
            let args = operations[op]
                .params
                .iter()
                .map(|default| match default {
                    Some(_) if g.below(2) == 0 => None,
                    _ => Some(number(g)),
                })
                .collect();
            Call { op, args }
        })
        .collect();
    Program { operations, calls }
}

/// The name written for a number, which is negative in half of the cases.
fn written(n: i32) -> String {
    n.to_string()
}

impl Program {
    /// The source: the effect, the handler, and a `main` that prints what each
    /// call gives, under the handler.
    fn source(&self) -> String {
        let mut text = String::from("use std.bits (i32)\nuse std.console (console, print)\n\n");
        text += "effect fx {\n";
        for (k, op) in self.operations.iter().enumerate() {
            let params: Vec<String> = (op.params.iter().enumerate())
                .map(|(i, d)| match d {
                    Some(d) => format!("a{i}: i32 = {}", written(*d)),
                    None => format!("a{i}: i32"),
                })
                .collect();
            text += &format!("  fn op{k}({}) -> i32\n", params.join(", "));
        }
        text += "}\n\nfn handler[T](body: fn() -> T ! fx) -> T {\n  fx(\n";
        for (k, op) in self.operations.iter().enumerate() {
            let names: Vec<String> = (0..op.params.len()).map(|i| format!("a{i}")).collect();
            let gives = match op.gives {
                Gives::Constant(n) => written(n),
                Gives::Argument(i) => format!("a{i}"),
            };
            // A lambda that takes nothing is a block alone.
            let head = if names.is_empty() {
                String::new()
            } else {
                format!("fn({}) ", names.join(", "))
            };
            text += &format!("    op{k}: {head}{{ {gives} }},\n");
        }
        text += "    body: body,\n  )\n}\n\nfn main() ! console, alloc {\n  with handler()\n";
        for call in &self.calls {
            let args: Vec<String> = (self.operations[call.op].params.iter().enumerate())
                .zip(&call.args)
                .filter_map(|((i, default), arg)| {
                    arg.map(|n| match default {
                        Some(_) => format!("a{i}: {}", written(n)),
                        None => written(n),
                    })
                })
                .collect();
            text += &format!("  print(\"{{op{}({})}}\")\n", call.op, args.join(", "));
        }
        text + "}\n"
    }

    /// What the program prints.
    fn prints(&self) -> Vec<u8> {
        let mut said = String::new();
        for call in &self.calls {
            let op = &self.operations[call.op];
            let n = match op.gives {
                Gives::Constant(n) => n,
                Gives::Argument(i) => call.args[i].or(op.params[i]).expect("given or defaulted"),
            };
            said += &format!("{n}\n");
        }
        said.into_bytes()
    }

    /// The same program with nothing to handle: each call is replaced by what
    /// it gives.
    fn direct(&self) -> String {
        let mut text = String::from("use std.bits (i32)\nuse std.console (console, print)\n\n");
        text += "fn main() ! console, alloc {\n";
        for line in String::from_utf8(self.prints()).unwrap().lines() {
            text += &format!("  print(\"{{{line}}}\")\n");
        }
        text + "}\n"
    }
}

fn outcome(source: &str) -> Outcome {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
    scalar::run(&program)
}

/// `reading` of the page: the code under the handler carries no effect, and
/// each call runs what the handler gave, with the defaults a call leaves out.
fn a_handler_that_gives_a_value_answers_each_call_of_its_operation_and_the_code_under_it_carries_no_effect()
{
    generate::check(7301, 30, program, |p| {
        let source = p.source();
        assert_eq!(outcome(&source).stdout, p.prints(), "for\n{source}");
    });
}

/// A body given where the parameter says `! fx` may carry `fx`, and the
/// function that gives it does not list it; what else the body does is
/// required of that function, where it is written.
fn a_body_given_to_a_handler_carries_its_effect_and_every_other_effect_is_required_where_it_is_written()
 {
    generate::check(7302, 30, |g: &mut Gen| (program(g), g.below(2) == 0), |(p, lists)| {
        let source = p.source();
        let listed = if *lists {
            source.clone()
        } else {
            source.replace("fn main() ! console, alloc {", "fn main() ! alloc {")
        };
        let found = check(listed.as_bytes()).err().unwrap_or_default();
        if *lists {
            assert!(found.is_empty(), "{found:?} for\n{listed}");
            return;
        }
        // The first `print` written under the handler is refused, at the call
        // and once, and what it says names the call and the effect.
        let first = listed.find("  print(").unwrap();
        let at = Pos {
            line: listed[..first].matches('\n').count() + 1,
            col: 3,
        };
        assert_eq!(found.iter().map(|e| e.pos).collect::<Vec<_>>(), vec![at], "for\n{listed}");
        assert!(found[0].message.contains("`print`") && found[0].message.contains("`console`"));
        assert!(found[0].message.contains("`main`"), "{found:?}");
    });
}

/// After the passes, a program with a handler holds the functions and the
/// calls that the same program holds with each operation written as what the
/// handler gives for it: the handler left no function, no call, and nothing
/// that tests which handler is in force.
fn a_call_of_an_operation_is_the_function_that_answers_it_lowered_in_place_and_leaves_no_call_behind()
 {
    let shape = |source: &str| {
        let built = scalar::passes(&lower(source.as_bytes()).expect("a program"));
        let calls = (built.functions.iter())
            .flat_map(|f| &f.blocks)
            .flat_map(|b| &b.instructions)
            .filter(|i| i.op.is_call())
            .count();
        (built.functions.len(), calls)
    };
    generate::check(7303, 20, program, |p| {
        assert_eq!(shape(&p.source()), shape(&p.direct()), "for\n{}", p.source());
    });
}

/// Every executable that can run here prints what the evaluator says.
fn every_executable_of_a_program_with_a_handler_prints_what_the_evaluator_says() {
    generate::check(7304, 6, program, |p| {
        let source = p.source();
        let said = outcome(&source);
        for target in targets::runnable() {
            targets::runs_as(source.as_bytes(), target, &said);
        }
    });
}
