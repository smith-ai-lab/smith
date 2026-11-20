//! A function that calls itself, where the call is not the last thing it
//! does, runs on a stack of its own that grows by chunks: the pass `recur`
//! makes a loop of the calls and keeps what each call needs after it returns
//! in memory. The functions are built by hand in the scalar form.

use smith::scalar::{self, Binary, Bits, Builder, Compare, Function, Op, Program, Type};

use crate::Test;
use crate::targets::{runnable, scalar_runs_as};

pub const ONCE: &[Test] = &[
    (
        "a_function_that_calls_itself_gives_the_same_with_a_stack_of_its_own_and_has_no_call_of_itself",
        a_function_that_calls_itself_gives_the_same_with_a_stack_of_its_own_and_has_no_call_of_itself,
    ),
    (
        "a_call_that_goes_a_hundred_thousand_deep_crosses_many_chunks_and_comes_back",
        a_call_that_goes_a_hundred_thousand_deep_crosses_many_chunks_and_comes_back,
    ),
    (
        "a_function_that_calls_itself_twice_keeps_what_it_needs_across_each_call",
        a_function_that_calls_itself_twice_keeps_what_it_needs_across_each_call,
    ),
    (
        "a_function_that_does_not_call_itself_and_one_that_calls_itself_last_are_not_changed",
        a_function_that_does_not_call_itself_and_one_that_calls_itself_last_are_not_changed,
    ),
];

pub const MACHINE: &[Test] = &[(
    "an_executable_of_a_function_that_calls_itself_a_million_deep_gives_the_right_number",
    an_executable_of_a_function_that_calls_itself_a_million_deep_gives_the_right_number,
)];

const LONG: Type = Type::Int(Bits::B64);

/// `depth(n)`: 0 when `n` is 0, and otherwise 1 more than `depth(n - 1)`.
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

/// `fib(n)`: `n` when it is below 2, and otherwise `fib(n - 1) + fib(n - 2)`, so
/// that what the first call gives is kept across the second.
fn fib() -> Function {
    let mut b = Builder::new();
    let [n] = b.params(&[LONG])[..] else {
        unreachable!()
    };
    let (calls, _) = b.block(&[]);
    let (stops, _) = b.block(&[]);
    let two = b.int(Bits::B64, 2);
    let small = b.compare(Compare::LessU, n, two);
    b.switch(small, vec![(calls, vec![]), (stops, vec![])]);
    b.at(calls);
    let one = b.int(Bits::B64, 1);
    let a = b.binary(Binary::Sub, n, one);
    let first = b.call(0, vec![a], &[LONG]);
    let c = b.binary(Binary::Sub, n, two);
    let second = b.call(0, vec![c], &[LONG]);
    let sum = b.binary(Binary::Add, first[0], second[0]);
    b.ret(vec![sum]);
    b.at(stops);
    b.ret(vec![n]);
    b.finish(&[LONG])
}

/// A program whose `main` ends with the low byte of `f(n)`.
fn program_of(f: Function, n: u64) -> Program {
    let mut b = Builder::new();
    let n = b.int(Bits::B64, n);
    let made = b.call(0, vec![n], &[LONG]);
    let status = b.narrow(made[0], Bits::B8);
    b.exit(status);
    b.ret(vec![]);
    Program {
        data: Vec::new(),
        functions: vec![f, b.finish(&[])],
        main: 1,
    }
}

fn calls_itself(f: &Function) -> bool {
    (f.blocks.iter().flat_map(|b| &b.instructions))
        .any(|i| matches!(&i.op, Op::Call { function, .. } if *function == 0))
}

fn a_function_that_calls_itself_gives_the_same_with_a_stack_of_its_own_and_has_no_call_of_itself() {
    for f in [depth(), fib()] {
        for n in [0, 1, 2, 3, 7, 12] {
            let program = program_of(f.clone(), n);
            assert_eq!(scalar::check(&program), Ok(()));
            let explicit = scalar::recur(program.clone());
            assert_eq!(scalar::check(&explicit), Ok(()), "n {n}");
            assert!(calls_itself(&program.functions[0]));
            assert!(!calls_itself(&explicit.functions[0]), "n {n}");
            assert_eq!(scalar::run(&explicit), scalar::run(&program), "n {n}");
        }
    }
}

fn a_call_that_goes_a_hundred_thousand_deep_crosses_many_chunks_and_comes_back() {
    let program = scalar::recur(program_of(depth(), 0));
    assert_eq!(scalar::check(&program), Ok(()));
    for n in [0u64, 1, 4_000, 4_100, 100_000] {
        assert_eq!(scalar::call(&program, 0, &[n]), vec![n], "n {n}");
    }
}

fn a_function_that_calls_itself_twice_keeps_what_it_needs_across_each_call() {
    let program = scalar::recur(program_of(fib(), 0));
    let expected = [0u64, 1, 1, 2, 3, 5, 8, 13, 21, 34, 55, 89, 144, 233, 377];
    for (n, want) in expected.iter().enumerate() {
        assert_eq!(scalar::call(&program, 0, &[n as u64]), vec![*want], "n {n}");
    }
}

fn a_function_that_does_not_call_itself_and_one_that_calls_itself_last_are_not_changed() {
    // `f(x)` is `x + 1`, and `main` is the `main` of a program.
    let mut b = Builder::new();
    let [x] = b.params(&[LONG])[..] else {
        unreachable!()
    };
    let one = b.int(Bits::B64, 1);
    let sum = b.binary(Binary::Add, x, one);
    b.ret(vec![sum]);
    let program = program_of(b.finish(&[LONG]), 5);
    let after = scalar::recur(program.clone());
    for (a, b) in program.functions.iter().zip(&after.functions) {
        assert_eq!(a.blocks, b.blocks);
        assert_eq!(a.types, b.types);
    }
}

fn an_executable_of_a_function_that_calls_itself_a_million_deep_gives_the_right_number() {
    const DEEP: u64 = 1_000_000;
    let program = scalar::recur(program_of(depth(), DEEP));
    let said = scalar::Outcome {
        stdout: Vec::new(),
        status: (DEEP & 0xff) as i32,
    };
    for target in runnable() {
        scalar_runs_as(&program, target, &said);
    }
}
