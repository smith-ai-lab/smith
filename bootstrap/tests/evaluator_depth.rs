//! The evaluator runs a call that goes deep, as a program that recurses may,
//! and does not take the stack of the evaluator for it: the open calls are
//! kept in a list.

use smith::scalar::{self, Binary, Bits, Builder, Compare, Function, Program, Type};

use crate::Test;

pub const ONCE: &[Test] = &[(
    "the_evaluator_runs_a_function_that_calls_itself_a_hundred_thousand_deep_and_returns_the_right_number",
    the_evaluator_runs_a_function_that_calls_itself_a_hundred_thousand_deep_and_returns_the_right_number,
)];

const LONG: Type = Type::Int(Bits::B64);

/// `depth(n)`: 0 when `n` is 0, and otherwise 1 more than `depth(n - 1)`, a
/// call that something is done after, so not a loop.
fn depth() -> Function {
    let mut b = Builder::new();
    let [n] = b.params(&[LONG])[..] else {
        unreachable!()
    };
    let (calls, _) = b.block(&[]);
    let (stops, _) = b.block(&[]);
    let zero = b.int(Bits::B64, 0);
    let is_zero = b.compare(Compare::Equal, n, zero);
    b.switch(is_zero, vec![(calls, vec![]), (stops, vec![])]);
    b.at(calls);
    let one = b.int(Bits::B64, 1);
    let before = b.binary(Binary::Sub, n, one);
    let made = b.call(0, vec![before], &[LONG]);
    let more = b.binary(Binary::Add, made[0], one);
    b.ret(vec![more]);
    b.at(stops);
    b.ret(vec![zero]);
    b.finish(&[LONG])
}

fn the_evaluator_runs_a_function_that_calls_itself_a_hundred_thousand_deep_and_returns_the_right_number()
{
    let program = Program {
        data: Vec::new(),
        functions: vec![depth()],
        main: 0,
    };
    for n in [0u64, 1, 2, 3, 1_000, 100_000] {
        assert_eq!(scalar::call(&program, 0, &[n]), vec![n]);
    }
}
