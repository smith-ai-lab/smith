//! A function whose name is given to a generic function that calls it through
//! `Function` asks the effects it carries of the handlers in force where its
//! name is written, however many generic functions it passes through.

use smith::lower;
use smith::scalar;

use crate::Test;
use crate::generate::{self, Gen};
use crate::targets;

pub const ONCE: &[Test] = &[(
    "a_function_called_through_function_asks_its_effects_of_the_handlers_where_its_name_is_written",
    a_function_called_through_function_asks_its_effects_of_the_handlers_where_its_name_is_written,
)];

/// The executables of the programs are checked against the evaluator.
pub const MACHINE: &[Test] = &[(
    "every_executable_calls_a_function_with_an_effect_through_function_as_the_evaluator_does",
    every_executable_calls_a_function_with_an_effect_through_function_as_the_evaluator_does,
)];

#[derive(Clone, Debug)]
struct Case {
    /// How many generic functions the name passes through.
    through: usize,
    /// What each of the two handlers gives, and which is nearer the call.
    inner: usize,
    outer: usize,
    asked: usize,
}

fn case(g: &mut Gen) -> Case {
    Case {
        through: g.below(3),
        inner: g.below(1000),
        outer: g.below(1000),
        asked: g.below(1000),
    }
}

fn program(c: &Case) -> (String, String) {
    let mut s = String::from(
        "use std.bits (u64)\nuse std.console (console, print)\n\nstruct Args {\n  id: u64\n}\n\neffect near {\n  fn find(id: u64) -> u64\n}\n\neffect far {\n  fn base() -> u64\n}\n\n",
    );
    s += &format!(
        "fn answer_near[T](body: fn() -> T ! near) -> T {{\n  near(find: fn(id) {{ {} }}, body: body)\n}}\n\nfn answer_far[T](body: fn() -> T ! far) -> T {{\n  far(base: {{ {} }}, body: body)\n}}\n\n",
        c.inner, c.outer
    );
    s += "fn asks(id: u64) -> (u64, u64) ! near, far {\n  (find(id), base())\n}\n\n";
    s += "fn call_it0[F, A, R](f: F, a: take A) -> R\n  requires Function[F, A, R]\n{\n  Function.call(f, a)\n}\n\n";
    let mut inner = "call_it0".to_string();
    for level in 1..=c.through {
        let name = format!("call_it{level}");
        s += &format!(
            "fn {name}[F, A, R](f: F, a: take A) -> R\n  requires Function[F, A, R]\n{{\n  {inner}(f, a)\n}}\n\n"
        );
        inner = name;
    }
    s += &format!(
        "fn main() ! console, alloc {{\n  with answer_far()\n  with answer_near()\n  (a, b) = {inner}(asks, Args(id: {}))\n  print(\"{{a}} {{b}}\")\n}}\n",
        c.asked
    );
    let said = format!("{} {}\n", c.inner, c.outer);
    (s, said)
}

fn a_function_called_through_function_asks_its_effects_of_the_handlers_where_its_name_is_written() {
    generate::check(8801, 8, case, |c| {
        let (source, said) = program(c);
        let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
        let out = scalar::run(&program);
        assert_eq!(String::from_utf8(out.stdout).unwrap(), said, "for\n{source}");
    });
}

fn every_executable_calls_a_function_with_an_effect_through_function_as_the_evaluator_does() {
    generate::check(8802, 2, case, |c| {
        let (source, said) = program(c);
        let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
        let ran = scalar::run(&program);
        assert_eq!(String::from_utf8(ran.stdout.clone()).unwrap(), said, "for\n{source}");
        for target in targets::runnable() {
            targets::runs_as(source.as_bytes(), target, &ran);
        }
    });
}
