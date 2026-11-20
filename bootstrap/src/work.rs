//! How many times the checker compares the two kinds of key its walks look
//! things up by: a definition of the file, and a copy of a function. A walk
//! that looks a key up in a list compares it with each key before it, so that
//! the count grows with the square of the program, and a walk that looks it up
//! by its hash compares it with one or two, so that the count grows with the
//! program. The count does not depend on the machine, as a time does, and is
//! read by `tests/build_scales.rs`.
//!
//! Threading counts too, the parameters and instructions of the blocks it goes
//! through, which `tests/pass_depth.rs` reads.

use std::cell::Cell;

thread_local! {
    static ITEMS: Cell<u64> = const { Cell::new(0) };
    static INSTANCES: Cell<u64> = const { Cell::new(0) };
    static GONE_THROUGH: Cell<u64> = const { Cell::new(0) };
}

/// Threading went through a block of this many parameters and instructions.
pub(crate) fn gone_through(size: usize) {
    GONE_THROUGH.set(GONE_THROUGH.get() + size as u64);
}

/// What the scalar passes have looked at so far, in this thread.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Looked {
    /// The parameters and instructions of the blocks that threading went
    /// through, one block each time a jump was sent past it.
    pub gone_through: u64,
}

/// What the scalar passes have looked at so far, in the thread that asks.
pub fn looked() -> Looked {
    Looked {
        gone_through: GONE_THROUGH.get(),
    }
}

/// A definition was compared with another.
pub(crate) fn item() {
    ITEMS.set(ITEMS.get() + 1);
}

/// A copy of a function was compared with another.
pub(crate) fn instance() {
    INSTANCES.set(INSTANCES.get() + 1);
}

/// How many comparisons of each kind this thread has made so far.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Compared {
    /// Definitions of a file, which `reach` and `recursion` look up.
    pub items: u64,
    /// Copies of a function, which `copies` and the lowering look up.
    pub instances: u64,
}

/// The comparisons made so far by the thread that asks.
pub fn compared() -> Compared {
    Compared {
        items: ITEMS.get(),
        instances: INSTANCES.get(),
    }
}
