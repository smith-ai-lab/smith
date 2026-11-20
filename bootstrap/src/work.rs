//! How many times the checker compares the two kinds of key its walks look
//! things up by: a definition of the file, and a copy of a function. A walk
//! that looks a key up in a list compares it with each key before it, so that
//! the count grows with the square of the program, and a walk that looks it up
//! by its hash compares it with one or two, so that the count grows with the
//! program. The count does not depend on the machine, as a time does, and is
//! read by `tests/build_scales.rs`.
//!
//! Inlining counts too, the parameters and instructions of the blocks it
//! renames, which `tests/pass_depth.rs` reads.

use std::cell::Cell;

thread_local! {
    static ITEMS: Cell<u64> = const { Cell::new(0) };
    static INSTANCES: Cell<u64> = const { Cell::new(0) };
    static MOVED: Cell<u64> = const { Cell::new(0) };
}

/// Inlining moved this many parameters and results of the blocks of a callee.
pub(crate) fn moved(size: usize) {
    MOVED.set(MOVED.get() + size as u64);
}

/// What the scalar passes have looked at so far, in this thread.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Looked {
    /// The parameters and results that inlining moved.
    pub moved: u64,
}

/// What the scalar passes have looked at so far, in the thread that asks.
pub fn looked() -> Looked {
    Looked { moved: MOVED.get() }
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
