//! What a list that grows asks of the handler of `alloc`, read from the
//! requests it makes, which `scalar::run_taking` gives in order.
//!
//! A list whose memory has 16 bytes or more first asks for nothing, which gives
//! where the free memory of its stack begins, and when that is the end of the
//! list it asks for the room it lacks, with `TRY` set, which the stack grants
//! only if its piece has room. A request that is not granted is followed by
//! the request for all the room, where the list is copied to, and none of
//! the two is for a list that is not the last memory taken.

use smith::scalar::handler::TRY as TRY_BIT;

const TRY: usize = TRY_BIT as usize;

/// How much room each growth gave a list, in order, and whether it was given
/// in place: the bytes the list's memory had room for after it, and whether
/// they were taken after the memory the list had.
pub fn growths(taken: &[usize]) -> Vec<(usize, bool)> {
    let mut grown = Vec::new();
    for (k, asked) in taken.iter().enumerate() {
        if *asked == 0 {
            continue;
        }
        if *asked & TRY == 0 {
            grown.push((*asked, false));
            continue;
        }
        // The room it lacks is as much as the list has, so it has twice that.
        let room = 2 * (*asked & !TRY);
        // A request that was not granted is followed by the one for the room.
        let refused = matches!(taken.get(k + 1), Some(next) if *next == room);
        if !refused {
            grown.push((room, true));
        }
    }
    grown
}

/// How many bytes of room each request that is not for nothing asked for,
/// in place or not, in order, as a list asked before it could grow in place.
pub fn rooms(taken: &[usize]) -> Vec<usize> {
    growths(taken).into_iter().map(|(room, _)| room).collect()
}

/// The bytes a program's lists copy when none grows in place, less those that
/// the growths in place did not copy, which are the half of each.
pub fn copied_in_place(taken: &[usize]) -> usize {
    growths(taken)
        .into_iter()
        .filter(|(_, in_place)| *in_place)
        .map(|(room, _)| room / 2)
        .sum()
}
