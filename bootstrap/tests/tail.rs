//! A function whose call of itself is the last thing it does is a loop in the
//! scalar form: the pass `tail` makes the call a jump, so that the executable
//! of a function that calls itself fifty million times does not grow its stack.
//! The functions are built by hand in the form, in each way the form can end
//! such a call, so what is tested is the pass and no more of the compiler.

use smith::scalar::{self, Binary, Bits, Builder, Compare, Function, Op, Program, Type};

use crate::Test;
use crate::targets::{runnable, scalar_runs_as};

pub const ONCE: &[Test] = &[
    (
        "a_function_that_calls_itself_last_has_no_call_of_itself_and_does_what_it_did",
        a_function_that_calls_itself_last_has_no_call_of_itself_and_does_what_it_did,
    ),
    (
        "a_call_of_itself_that_something_is_done_after_stays_a_call",
        a_call_of_itself_that_something_is_done_after_stays_a_call,
    ),
    (
        "a_function_with_no_call_of_itself_and_main_are_not_changed",
        a_function_with_no_call_of_itself_and_main_are_not_changed,
    ),
];

pub const MACHINE: &[Test] = &[(
    "an_executable_of_a_function_that_calls_itself_last_runs_a_million_rounds_in_its_own_frame",
    an_executable_of_a_function_that_calls_itself_last_runs_a_million_rounds_in_its_own_frame,
)];

const LONG: Type = Type::Int(Bits::B64);

/// How a function that calls itself last is built.
#[derive(Clone, Copy, Debug)]
struct Shape {
    /// The call is followed by a block that hands its result on, as the end of
    /// an `if` is, and not by a return.
    through_a_block: bool,
    /// Which case of the choice on `n == 0` calls itself: the first is for `n`
    /// not zero.
    call_in_case: usize,
    /// How the number given on is made of it and `n`.
    op: Binary,
    /// The function gives back the number and a second, which is `n` as the
    /// call was made with it.
    two_results: bool,
    /// Another block, which holds nothing, is between the call and the return.
    two_blocks: bool,
}

fn shapes() -> Vec<Shape> {
    let mut all = Vec::new();
    for through_a_block in [false, true] {
        for call_in_case in [0, 1] {
            for op in [Binary::Add, Binary::Xor, Binary::Mul] {
                for two_results in [false, true] {
                    for two_blocks in [false, true] {
                        if two_blocks && !through_a_block {
                            continue;
                        }
                        all.push(Shape {
                            through_a_block,
                            call_in_case,
                            op,
                            two_results,
                            two_blocks,
                        });
                    }
                }
            }
        }
    }
    all
}

/// `f(n, acc)`: when `n` is 0, `acc`, and otherwise `f(n - 1, acc op n)`,
/// built as `shape` says. The function is number 0 of the program.
fn counting(shape: Shape) -> Function {
    let mut b = Builder::new();
    let [n, acc] = b.params(&[LONG, LONG])[..] else {
        unreachable!()
    };
    let (calls, _) = b.block(&[]);
    let (stops, _) = b.block(&[]);
    let results: Vec<Type> = if shape.two_results { vec![LONG, LONG] } else { vec![LONG] };
    let (join, joined) = b.block(&results);
    let (second, second_params) = b.block(&results);
    let zero = b.int(Bits::B64, 0);
    let is_zero = b.compare(Compare::Equal, n, zero);
    // The choice is on `n == 0`, which is 1 when `n` is zero, and on `n != 0`
    // when the call is in the second case.
    if shape.call_in_case == 0 {
        b.switch(is_zero, vec![(calls, vec![]), (stops, vec![])]);
    } else {
        let not_zero = b.compare(Compare::LessU, zero, n);
        b.switch(not_zero, vec![(stops, vec![]), (calls, vec![])]);
    }
    b.at(calls);
    let one = b.int(Bits::B64, 1);
    let before = b.binary(Binary::Sub, n, one);
    let given = b.binary(shape.op, acc, n);
    let made = b.call(0, vec![before, given], &results);
    match (shape.through_a_block, shape.two_blocks) {
        (false, _) => b.ret(made),
        (true, false) => b.jump(join, made),
        (true, true) => b.jump(second, made),
    }
    b.at(stops);
    let mut gives = vec![acc];
    if shape.two_results {
        gives.push(n);
    }
    if shape.through_a_block {
        b.jump(join, gives);
    } else {
        b.ret(gives);
    }
    b.at(second);
    b.jump(join, second_params);
    b.at(join);
    b.ret(joined);
    b.finish(&results)
}

/// A program whose `main` ends with the low byte of `f(n, 0)`.
fn program_of(f: Function, n: u64) -> Program {
    let results = f.results.clone();
    let mut b = Builder::new();
    let n = b.int(Bits::B64, n);
    let zero = b.int(Bits::B64, 0);
    let made = b.call(0, vec![n, zero], &results);
    let status = b.narrow(made[0], Bits::B8);
    b.exit(status);
    b.ret(vec![]);
    Program {
        data: Vec::new(),
        functions: vec![f, b.finish(&[])],
        main: 1,
    }
}

fn calls_itself(f: &Function, index: usize) -> bool {
    (f.blocks.iter().flat_map(|b| &b.instructions))
        .any(|i| matches!(&i.op, Op::Call { function, .. } if *function == index))
}

fn a_function_that_calls_itself_last_has_no_call_of_itself_and_does_what_it_did() {
    for shape in shapes() {
        for n in [0, 1, 2, 7, 40] {
            let program = program_of(counting(shape), n);
            assert_eq!(scalar::check(&program), Ok(()), "{shape:?}");
            let looped = scalar::tail(program.clone());
            assert_eq!(scalar::check(&looped), Ok(()), "{shape:?} after the pass");
            assert!(!calls_itself(&looped.functions[0], 0), "{shape:?}");
            assert!(calls_itself(&program.functions[0], 0), "{shape:?} is no case");
            assert_eq!(scalar::run(&looped), scalar::run(&program), "{shape:?}, n {n}");
            let passed = scalar::passes(&program);
            assert!(!calls_itself(&passed.functions[0], 0), "{shape:?}");
            assert_eq!(scalar::run(&passed), scalar::run(&program), "{shape:?}, n {n}");
        }
    }
}

/// `f(n, acc)` that adds 1 to what the call gives, so that the call is not the
/// last thing it does.
fn counting_then_one_more() -> Function {
    let mut b = Builder::new();
    let [n, acc] = b.params(&[LONG, LONG])[..] else {
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
    let made = b.call(0, vec![before, acc], &[LONG]);
    let more = b.binary(Binary::Add, made[0], one);
    b.ret(vec![more]);
    b.at(stops);
    b.ret(vec![acc]);
    b.finish(&[LONG])
}

/// `f(n, acc)` that gives `acc` on to a block that other jumps give a number
/// to as well, so that what the call gives is not all the block gives.
fn counting_whose_result_is_read_twice() -> Function {
    let mut b = Builder::new();
    let [n, acc] = b.params(&[LONG, LONG])[..] else {
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
    let made = b.call(0, vec![before, acc], &[LONG]);
    let twice = b.binary(Binary::Add, made[0], made[0]);
    b.ret(vec![twice]);
    b.at(stops);
    b.ret(vec![acc]);
    b.finish(&[LONG])
}

fn a_call_of_itself_that_something_is_done_after_stays_a_call() {
    for f in [counting_then_one_more(), counting_whose_result_is_read_twice()] {
        for n in [0, 3, 9] {
            let program = program_of(f.clone(), n);
            let looped = scalar::tail(program.clone());
            assert!(calls_itself(&looped.functions[0], 0));
            assert_eq!(looped.functions[0].blocks, program.functions[0].blocks);
            assert_eq!(scalar::run(&looped), scalar::run(&program));
        }
    }
}

fn a_function_with_no_call_of_itself_and_main_are_not_changed() {
    let mut b = Builder::new();
    let [x] = b.params(&[LONG])[..] else {
        unreachable!()
    };
    let one = b.int(Bits::B64, 1);
    let sum = b.binary(Binary::Add, x, one);
    b.ret(vec![sum]);
    let add_one = b.finish(&[LONG]);
    let mut m = Builder::new();
    let five = m.int(Bits::B64, 5);
    let made = m.call(0, vec![five], &[LONG]);
    let status = m.narrow(made[0], Bits::B8);
    m.exit(status);
    m.ret(vec![]);
    let program = Program {
        data: Vec::new(),
        functions: vec![add_one, m.finish(&[])],
        main: 1,
    };
    let after = scalar::tail(program.clone());
    for (a, b) in program.functions.iter().zip(&after.functions) {
        assert_eq!(a.blocks, b.blocks);
        assert_eq!(a.types, b.types);
    }
}

/// The function that adds up `n`, `n - 1`, and so on, in `acc`, called a
/// million times from itself, which a stack of frames of any size that a call
/// needs would not hold.
fn an_executable_of_a_function_that_calls_itself_last_runs_a_million_rounds_in_its_own_frame() {
    const ROUNDS: u64 = 1_000_000;
    let shape = Shape {
        through_a_block: true,
        call_in_case: 0,
        op: Binary::Add,
        two_results: false,
        two_blocks: false,
    };
    let looped = scalar::tail(program_of(counting(shape), ROUNDS));
    // What the evaluator says for the smaller programs above is what the sum
    // gives, so for this one the sum is what is said.
    let said = scalar::Outcome {
        stdout: Vec::new(),
        status: ((ROUNDS * (ROUNDS + 1) / 2) & 0xff) as i32,
    };
    for target in runnable() {
        scalar_runs_as(&looped, target, &said);
    }
}
