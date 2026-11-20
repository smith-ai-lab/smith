//! `std.http2` is held to the text of RFC 9113 sections 4 to 6 and of RFC 7541
//! sections 2 to 6, which are in `reference/rfc/`: every sentence that holds MUST,
//! MUST NOT, or SHALL is in a line of a ledger, `http2.ledger` and `hpack.ledger`, with the
//! test that holds it or the reason it binds nothing the module does.

use crate::Test;
use crate::standard;

pub const ONCE: &[Test] = &[
    (
        "every_must_of_rfc_9113_sections_4_to_6_is_in_the_ledger_of_http2",
        every_must_of_rfc_9113_sections_4_to_6_is_in_the_ledger_of_http2,
    ),
    (
        "every_must_of_rfc_7541_sections_2_to_6_is_in_the_ledger_of_hpack",
        every_must_of_rfc_7541_sections_2_to_6_is_in_the_ledger_of_hpack,
    ),
];

const FRAMES: &[&str] = &["4", "5", "6"];
const HEADERS: &[&str] = &["2", "3", "4", "5", "6"];
const FRAME_LEDGER: &str = include_str!("http2.ledger");
const HEADER_LEDGER: &str = include_str!("hpack.ledger");

/// The names of every test that there is.
fn test_names() -> Vec<&'static str> {
    crate::once_tests()
        .iter()
        .chain(crate::machine_tests().iter())
        .map(|(name, _)| *name)
        .collect()
}

fn every_must_of_rfc_9113_sections_4_to_6_is_in_the_ledger_of_http2() {
    let rfc = standard::read("rfc9113.txt");
    let tests = test_names();
    if let Err(errors) = standard::check_ledger(&rfc, FRAMES, FRAME_LEDGER, &tests) {
        panic!("{}", errors.join("\n"));
    }
}

fn every_must_of_rfc_7541_sections_2_to_6_is_in_the_ledger_of_hpack() {
    let rfc = standard::read("rfc7541.txt");
    let tests = test_names();
    if let Err(errors) = standard::check_ledger(&rfc, HEADERS, HEADER_LEDGER, &tests) {
        panic!("{}", errors.join("\n"));
    }
}
