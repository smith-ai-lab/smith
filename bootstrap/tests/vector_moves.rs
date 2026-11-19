//! Copying values to new places as if all were copied at once, when the values
//! are not all as wide: a vector takes 16 bytes and a float 8, and a place that
//! one leaves the other may take. What is set aside to break a circle is the whole
//! of what a place holds.

use smith::target::moves::{Move, Step, sequence};

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[(
    "copying_values_of_different_widths_as_if_all_at_once_keeps_every_byte",
    copying_values_of_different_widths_as_if_all_at_once_keeps_every_byte,
)];

/// The bytes of a place, `width` of them.
fn bytes(n: u128, width: u32) -> u128 {
    if width >= 16 {
        n
    } else {
        n & ((1u128 << (8 * width)) - 1)
    }
}

/// Does the steps on places that hold a number each, with one more place that
/// holds what was set aside, of the width it was set aside at.
fn run(places: &mut [u128], steps: &[Step<usize>]) {
    let mut aside = None;
    for step in steps {
        match step {
            Step::Copy { to, from, width } => places[*to] = bytes(places[*from], *width),
            Step::Save { from, width } => {
                assert!(aside.is_none(), "one is set aside at a time");
                aside = Some(bytes(places[*from], *width));
            }
            Step::Restore { to, width } => {
                places[*to] = bytes(aside.take().expect("it was set aside"), *width)
            }
        }
    }
    assert!(aside.is_none(), "what was set aside is put back");
}

fn copying_values_of_different_widths_as_if_all_at_once_keeps_every_byte() {
    let generate = |g: &mut Gen| {
        let places = 2 + g.below(9);
        // What each place holds is 8 bytes or 16, and what it is given is as wide
        // as what it is given from, so a place that held a float may be given a
        // vector.
        let widths: Vec<u32> = (0..places).map(|_| 8 * (1 + g.below(2) as u32)).collect();
        let mut to: Vec<usize> = (0..places).collect();
        for i in (1..places).rev() {
            to.swap(i, g.below(i + 1));
        }
        let mut moves: Vec<(usize, usize)> = Vec::new();
        for to in to.into_iter().take(1 + g.below(places)) {
            moves.push((to, g.below(places)));
        }
        (widths, moves)
    };
    generate::check(8135, 2000, generate, |(widths, moves)| {
        let before: Vec<u128> = widths
            .iter()
            .enumerate()
            .map(|(n, w)| bytes(0x0123_4567_89ab_cdef_0f1e_2d3c_4b5a_6978 + n as u128, *w))
            .collect();
        let mut after = before.clone();
        for (to, from) in moves {
            after[*to] = before[*from];
        }
        let moves: Vec<Move<usize>> = moves
            .iter()
            .map(|(to, from)| Move {
                to: *to,
                from: *from,
                width: widths[*from],
            })
            .collect();
        let mut done = before.clone();
        run(&mut done, &sequence(&moves));
        assert_eq!(done, after);
    });
}
