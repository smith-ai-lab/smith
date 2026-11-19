//! `std.x509` is held to the text of RFC 5280, which is in `reference/rfc/`:
//! every sentence of sections 4.1 and 4.2 that holds MUST, MUST NOT, or SHALL
//! is in a line of the ledger, `x509.ledger`, with the test that holds it or the
//! reason it binds nothing a reader that checks nothing does.

use crate::Test;
use crate::standard;

pub const ONCE: &[Test] = &[(
    "every_must_of_rfc_5280_sections_4_1_and_4_2_is_in_the_ledger_of_x509",
    every_must_of_rfc_5280_sections_4_1_and_4_2_is_in_the_ledger_of_x509,
)];

const SECTIONS: &[&str] = &["4.1", "4.2"];
const LEDGER: &str = include_str!("x509.ledger");

fn every_must_of_rfc_5280_sections_4_1_and_4_2_is_in_the_ledger_of_x509() {
    let rfc = standard::read("rfc5280.txt");
    let tests: Vec<&str> = crate::once_tests()
        .iter()
        .chain(crate::machine_tests().iter())
        .map(|(name, _)| *name)
        .collect();
    if let Err(errors) = standard::check_ledger(&rfc, SECTIONS, LEDGER, &tests) {
        panic!("{}", errors.join("\n"));
    }
}
