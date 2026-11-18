//! What the register allocator gives that its other tests leave out, because
//! no function they build or take from a program shows it: a block that no
//! jump reaches, which reads what another block made.

use smith::scalar::{Binary, Bits, Builder, Function, Type};
use smith::target::alloc::{Home, Registers, allocate};

use crate::Test;

pub const ONCE: &[Test] = &[(
    "a_value_is_let_go_after_its_last_read_in_a_block_no_jump_reaches",
    a_value_is_let_go_after_its_last_read_in_a_block_no_jump_reaches,
)];

/// A function of two blocks. The first makes a number, reads it, and returns. The second,
/// which nothing jumps to, reads that number once, first, and then holds
/// `held` numbers that it makes, all alive at its end.
fn reads_in_a_block_no_jump_reaches(held: usize) -> Function {
    let int = Type::Int(Bits::B64);
    let mut b = Builder::new();
    let (unreached, _) = b.block(&[]);
    let made = b.int(Bits::B64, 7);
    b.binary(Binary::Add, made, made);
    b.ret(vec![]);
    b.at(unreached);
    let mut alive = vec![b.binary(Binary::Add, made, made)];
    for n in 1..held {
        alive.push(b.int(Bits::B64, n as u64));
    }
    b.ret(alive);
    b.finish(&vec![int; held])
}

/// The registers a function that holds `held` numbers at once is enough for.
fn just_enough(held: usize) -> Registers {
    Registers {
        changed: held,
        preserved: 0,
        passed: 0,
    }
}

fn a_value_is_let_go_after_its_last_read_in_a_block_no_jump_reaches() {
    for held in 1..6 {
        let f = reads_in_a_block_no_jump_reaches(held);
        let a = allocate(&f, just_enough(held));
        assert_eq!(a.frame, 0, "{held} numbers held, and the registers for them");
        assert!(a.homes.iter().all(|h| matches!(h, Home::Register(_))));
    }
}
