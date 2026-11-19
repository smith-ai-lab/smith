//! A function that calls nothing and has a loop is offered every register, so
//! its loop keeps in registers what a loop of a function without one would
//! send to the frame, and a function without a loop is not offered the
//! registers that it would have to save.

use smith::scalar::{Binary, Bits, Builder, Compare, Function};
use smith::target::alloc::{Allocation, Home, Registers, allocate};

use crate::Test;

pub const ONCE: &[Test] = &[
    (
        "a_loop_that_calls_nothing_keeps_its_values_in_registers_that_a_function_without_one_would_spill",
        a_loop_that_calls_nothing_keeps_its_values_in_registers_that_a_function_without_one_would_spill,
    ),
    (
        "a_loop_that_calls_something_holds_what_is_alive_across_the_call_in_registers_a_call_does_not_change",
        a_loop_that_calls_something_holds_what_is_alive_across_the_call_in_registers_a_call_does_not_change,
    ),
];

const CHIP: Registers = Registers {
    changed: 5,
    preserved: 6,
    passed: 5,
};

/// A function that makes `held` numbers, then goes round a block that adds
/// them all to a counter, back to itself when `back`, and otherwise on to a
/// block that returns, and that calls a function in the loop when `calls`.
fn function(held: usize, back: bool, calls: bool) -> Function {
    let mut b = Builder::new();
    let (_, counter) = b.block(&[smith::scalar::Type::Int(Bits::B64)]);
    let (done, _) = b.block(&[]);
    let kept: Vec<_> = (0..held).map(|n| b.int(Bits::B64, n as u64 + 7)).collect();
    let first = b.int(Bits::B64, 0);
    b.jump(1, vec![first]);
    b.at(1);
    let mut sum = counter[0];
    for v in &kept {
        sum = b.binary(Binary::Add, sum, *v);
    }
    if calls {
        b.call(0, vec![], &[]);
    }
    if back {
        let limit = b.int(Bits::B64, 1000);
        let less = b.compare(Compare::LessU, sum, limit);
        b.switch(less, vec![(done, vec![]), (1, vec![sum])]);
    } else {
        b.jump(done, vec![]);
    }
    b.at(done);
    b.ret(vec![]);
    b.finish(&[])
}

fn allocation(f: &Function) -> Allocation {
    allocate(f, CHIP, &vec![false; f.types.len()])
}

fn in_the_frame(a: &Allocation) -> usize {
    a.homes.iter().filter(|h| matches!(h, Home::Frame(_))).count()
}

fn a_loop_that_calls_nothing_keeps_its_values_in_registers_that_a_function_without_one_would_spill()
{
    // More values are alive in the loop than a call may change, and fewer
    // than the chip has.
    let held = CHIP.changed + 2;
    let with_a_loop = allocation(&function(held, true, false));
    assert_eq!(in_the_frame(&with_a_loop), 0, "{with_a_loop:?}");
    assert!(!with_a_loop.saved.is_empty());
    for r in &with_a_loop.saved {
        assert!(CHIP.is_preserved(*r));
    }
    let without = allocation(&function(held, false, false));
    assert!(in_the_frame(&without) > 0, "{without:?}");
    assert!(without.saved.is_empty());
}

fn a_loop_that_calls_something_holds_what_is_alive_across_the_call_in_registers_a_call_does_not_change()
{
    let across = allocation(&function(CHIP.changed + 2, true, true));
    assert!(!across.saved.is_empty());
}
