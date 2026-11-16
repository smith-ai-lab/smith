//! Copying values to new places as if all were copied at once, which every
//! chip needs where a jump gives a block its parameters. The places are
//! registers and places in the frame, and the chip says how to copy between
//! them.

/// A value to copy: `width` bytes from `from` to `to`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Move<L> {
    pub to: L,
    pub from: L,
    pub width: u32,
}

/// One copy of the sequence, in the order they are done.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Step<L> {
    /// Copies from a place to another.
    Copy { to: L, from: L, width: u32 },
    /// Holds what is in `from` aside, before it is overwritten. At most one
    /// value is held at a time.
    Save { from: L, width: u32 },
    /// Copies what was held aside to `to`.
    Restore { to: L, width: u32 },
}

/// Orders the moves so that doing the steps one after another leaves every
/// place with what it would have after copying all at once. A move whose
/// place is read by no other goes first. When every place left is still to be
/// read, they go round in a circle, and holding one aside breaks it.
pub fn sequence<L: Copy + PartialEq>(moves: &[Move<L>]) -> Vec<Step<L>> {
    // `None` as a source is what was held aside.
    let mut left: Vec<(L, Option<L>, u32)> = moves
        .iter()
        .filter(|m| m.to != m.from)
        .map(|m| (m.to, Some(m.from), m.width))
        .collect();
    let mut steps = Vec::new();
    while !left.is_empty() {
        let free = (0..left.len()).find(|&i| left.iter().all(|m| m.1 != Some(left[i].0)));
        match free {
            Some(i) => {
                let (to, from, width) = left.remove(i);
                steps.push(match from {
                    Some(from) => Step::Copy { to, from, width },
                    None => Step::Restore { to, width },
                });
            }
            None => {
                let (place, _, width) = left[0];
                steps.push(Step::Save { from: place, width });
                for m in &mut left {
                    if m.1 == Some(place) {
                        m.1 = None;
                    }
                }
            }
        }
    }
    steps
}
