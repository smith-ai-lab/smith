//! How much a piece of work allocates, which does not depend on the machine
//! as its time does. Every allocation of the thread that asks is counted, and
//! the work of other threads is not, so tests that run side by side do not
//! count each other's.

use std::alloc::{GlobalAlloc, Layout, System};
use std::cell::Cell;

thread_local! {
    static CALLS: Cell<u64> = const { Cell::new(0) };
    static BYTES: Cell<u64> = const { Cell::new(0) };
}

struct Counting;

fn count(bytes: usize) {
    let _ = CALLS.try_with(|c| c.set(c.get() + 1));
    let _ = BYTES.try_with(|b| b.set(b.get() + bytes as u64));
}

// SAFETY: every method does what `System` does, and counts first.
unsafe impl GlobalAlloc for Counting {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        count(layout.size());
        unsafe { System.alloc(layout) }
    }

    unsafe fn alloc_zeroed(&self, layout: Layout) -> *mut u8 {
        count(layout.size());
        unsafe { System.alloc_zeroed(layout) }
    }

    unsafe fn dealloc(&self, pointer: *mut u8, layout: Layout) {
        unsafe { System.dealloc(pointer, layout) }
    }

    unsafe fn realloc(&self, pointer: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
        count(new_size);
        unsafe { System.realloc(pointer, layout, new_size) }
    }
}

#[global_allocator]
static ALLOCATOR: Counting = Counting;

/// How many times, and how many bytes in all, `work` asked to allocate.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Allocated {
    pub calls: u64,
    pub bytes: u64,
}

/// What `work` gives, and what it allocated.
pub fn allocated<T>(work: impl FnOnce() -> T) -> (T, Allocated) {
    let (calls, bytes) = (CALLS.get(), BYTES.get());
    let result = work();
    let counted = Allocated {
        calls: CALLS.get() - calls,
        bytes: BYTES.get() - bytes,
    };
    (result, counted)
}
