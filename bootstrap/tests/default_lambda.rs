//! A parameter that takes a function may have a default, a lambda that uses no
//! parameter and carries only the effects the type of the parameter lists. A
//! call that leaves it out gets it where the call is written, so it costs what
//! the same call with the lambda written out costs. The expectation of each
//! program is what the generator that wrote its calls says they print.

use smith::diag::Error;
use smith::scalar::{self, Outcome};
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::targets;

pub const ONCE: &[Test] = &[
    (
        "a_call_gets_the_default_lambda_exactly_when_it_leaves_the_function_out",
        a_call_gets_the_default_lambda_exactly_when_it_leaves_the_function_out,
    ),
    (
        "a_default_lambda_that_uses_a_parameter_or_carries_an_effect_the_type_does_not_list_is_refused",
        a_default_lambda_that_uses_a_parameter_or_carries_an_effect_the_type_does_not_list_is_refused,
    ),
    (
        "a_parameter_with_a_default_lambda_still_comes_after_those_without_one_unless_it_is_the_last",
        a_parameter_with_a_default_lambda_still_comes_after_those_without_one_unless_it_is_the_last,
    ),
    (
        "a_call_that_leaves_the_function_out_is_the_instructions_of_the_call_that_writes_the_default",
        a_call_that_leaves_the_function_out_is_the_instructions_of_the_call_that_writes_the_default,
    ),
    (
        "a_function_with_a_default_lambda_is_passed_as_a_value_exactly_as_one_without_it_is",
        a_function_with_a_default_lambda_is_passed_as_a_value_exactly_as_one_without_it_is,
    ),
];

/// The executables of the programs are checked against the evaluator.
pub const MACHINE: &[Test] = &[(
    "every_executable_of_a_call_that_gets_a_default_lambda_prints_what_the_evaluator_says",
    every_executable_of_a_call_that_gets_a_default_lambda_prints_what_the_evaluator_says,
)];

fn errors(source: &str) -> Vec<Error> {
    check(source.as_bytes()).err().unwrap_or_default()
}

fn outcome(source: &str) -> Outcome {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
    scalar::run(&program)
}

/// A call of `p`: the text it gives for `inside` and for `class`, if it gives
/// them, and the block after it, if it has one.
#[derive(Clone, Debug)]
struct Call {
    inside: Option<String>,
    class: Option<String>,
    block: Option<String>,
}

fn word(g: &mut Gen) -> String {
    (0..g.below(3) + 1)
        .map(|_| (b'a' + g.below(26) as u8) as char)
        .collect()
}

fn maybe(g: &mut Gen) -> Option<String> {
    (g.below(2) == 0).then(|| word(g))
}

fn calls(g: &mut Gen) -> Vec<Call> {
    (0..g.below(4) + 1)
        .map(|_| Call {
            inside: maybe(g),
            class: maybe(g),
            block: maybe(g),
        })
        .collect()
}

/// The program: `p` writes its `class` and `inside`, then runs its function,
/// whose default writes `D`.
fn source(calls: &[Call]) -> String {
    let mut text = String::from(
        "use std.console (console, print)\n\n\
         effect markup {\n  fn emit(text: str)\n}\n\n\
         fn p(inside: str = \"i\", class: str = \"c\", body: fn() ! markup = { emit(\"D\") }) ! markup {\n  \
         emit(class)\n  emit(inside)\n  body()\n}\n\n\
         fn page() ! markup {\n",
    );
    for call in calls {
        let mut args = Vec::new();
        if let Some(inside) = &call.inside {
            args.push(format!("\"{inside}\""));
        }
        if let Some(class) = &call.class {
            args.push(format!("class: \"{class}\""));
        }
        text += &format!("  p({})", args.join(", "));
        if let Some(block) = &call.block {
            text += &format!(" {{ emit(\"{block}\") }}");
        }
        text += "\n";
    }
    text + "}\n\n\
        fn main() ! console {\n  markup(emit: fn(t) { print(t) }, body: { page() })\n}\n"
}

fn prints(calls: &[Call]) -> Vec<u8> {
    let mut said = String::new();
    for call in calls {
        let class = call.class.as_deref().unwrap_or("c");
        let inside = call.inside.as_deref().unwrap_or("i");
        let block = call.block.as_deref().unwrap_or("D");
        said += &format!("{class}\n{inside}\n{block}\n");
    }
    said.into_bytes()
}

/// A call with the text, with a block, with both and with neither, in any
/// order and number, is given its own text and block where it writes them and
/// the default of each where it does not.
fn a_call_gets_the_default_lambda_exactly_when_it_leaves_the_function_out() {
    generate::check(7701, 40, calls, |c| {
        let program = source(c);
        assert_eq!(outcome(&program).stdout, prints(c), "for\n{program}");
    });
}

/// A default of a parameter that takes a function is held to the rules of any
/// default, and carries the effects the type of the parameter lists and no other.
fn a_default_lambda_that_uses_a_parameter_or_carries_an_effect_the_type_does_not_list_is_refused() {
    let program = |default: &str| {
        format!(
            "use std.console (console, print)\n\n\
             effect markup {{\n  fn emit(text: str)\n}}\n\n\
             fn p(inside: str, body: fn() ! markup = {default}) ! markup {{\n  body()\n}}\n\n\
             fn main() ! console {{\n  markup(emit: fn(t) {{ print(t) }}, body: {{ p(\"x\") }})\n}}\n"
        )
    };
    assert!(errors(&program("{ emit(\"D\") }")).is_empty());
    let uses = errors(&program("{ emit(inside) }"));
    assert!(
        uses.iter()
            .any(|e| e.message == "a default uses no parameter, and `inside` is one"),
        "{uses:?}"
    );
    let other = errors(&program("{ print(\"D\") }"));
    assert!(
        other
            .iter()
            .any(|e| e.message == "a default carries no effect, and `print` needs the `console` effect"),
        "{other:?}"
    );
}

/// A parameter with a default comes after those without one, and a final
/// parameter that takes a function may come after them without one.
fn a_parameter_with_a_default_lambda_still_comes_after_those_without_one_unless_it_is_the_last() {
    let program = |params: &str| {
        format!(
            "use std.bits (i32)\nuse std.console (console, print)\n\n\
             fn f({params}) -> i32 {{ body() }}\n\n\
             fn main() ! console, alloc {{\n  print(\"{{f(1)}}\")\n}}\n"
        )
    };
    let after = errors(&program("body: fn() -> i32 = { 1 }, n: i32"));
    assert!(
        after
            .iter()
            .any(|e| e.message == "`n` has no default, and comes after `body`, which has one"),
        "{after:?}"
    );
    let first = errors(&program("n: i32, body: fn() -> i32 = { 1 }"));
    assert!(first.is_empty(), "{first:?}");
}

/// What the passes leave of a program: how many functions, and how many
/// instructions in them.
fn shape(source: &str) -> (usize, usize) {
    let built = scalar::passes(&lower(source.as_bytes()).expect("a program"));
    let instructions: usize = (built.functions.iter())
        .flat_map(|f| &f.blocks)
        .map(|b| b.instructions.len())
        .sum();
    (built.functions.len(), instructions)
}

/// The default is filled in where the call is written, so a call that leaves
/// the function out is the call with the same lambda written after it.
fn a_call_that_leaves_the_function_out_is_the_instructions_of_the_call_that_writes_the_default() {
    generate::check(7702, 30, |g: &mut Gen| (word(g), word(g)), |(inside, default)| {
        let program = |call: &str| {
            format!(
                "use std.console (console, print)\n\n\
                 effect markup {{\n  fn emit(text: str)\n}}\n\n\
                 fn p(inside: str, body: fn() ! markup = {{ emit(\"{default}\") }}) ! markup {{\n  \
                 emit(inside)\n  body()\n}}\n\n\
                 fn main() ! console {{\n  markup(emit: fn(t) {{ print(t) }}, body: {{ {call} }})\n}}\n"
            )
        };
        let left_out = program(&format!("p(\"{inside}\")"));
        let written = program(&format!("p(\"{inside}\") {{ emit(\"{default}\") }}"));
        assert_eq!(shape(&left_out), shape(&written), "for\n{left_out}\nand\n{written}");
        assert_eq!(outcome(&left_out).stdout, outcome(&written).stdout);
    });
}

/// A function that takes a function is not passed as a value yet, with a
/// default or without one, and the refusal is the same.
fn a_function_with_a_default_lambda_is_passed_as_a_value_exactly_as_one_without_it_is() {
    let program = |default: &str| {
        format!(
            "use std.bits (i32)\nuse std.console (console, print)\n\n\
             fn r(a: i32, body: fn(i32) -> i32{default}) -> i32 {{ body(a) }}\n\n\
             fn apply(f: fn(i32, fn(i32) -> i32) -> i32, v: i32) -> i32 {{ v }}\n\n\
             fn main() ! console, alloc {{\n  print(\"{{apply(r, 3)}}\")\n}}\n"
        )
    };
    let with = errors(&program(" = fn(x) { x }"));
    let without = errors(&program(""));
    assert!(!with.is_empty());
    let messages = |e: &[Error]| e.iter().map(|e| e.message.clone()).collect::<Vec<_>>();
    assert_eq!(messages(&with), messages(&without));
}

/// Every executable that can run here prints what the evaluator says.
fn every_executable_of_a_call_that_gets_a_default_lambda_prints_what_the_evaluator_says() {
    generate::check(7703, 6, calls, |c| {
        let program = source(c);
        let said = outcome(&program);
        for target in targets::runnable() {
            targets::runs_as(program.as_bytes(), target, &said);
        }
    });
}
