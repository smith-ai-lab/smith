//! `pem` of `std.x509` is held to the text of RFC 7468, which is in
//! `reference/rfc/`: every sentence of sections 2, 3, and 5.1 that holds MUST,
//! MUST NOT, or SHALL is in a line of the ledger, `pem.ledger`, with the test that
//! holds it or the reason it binds nothing a reader does.

use crate::Test;
use crate::standard;

pub const ONCE: &[Test] = &[(
    "every_must_of_rfc_7468_sections_2_3_and_5_1_is_in_the_ledger_of_pem",
    every_must_of_rfc_7468_sections_2_3_and_5_1_is_in_the_ledger_of_pem,
)];

const SECTIONS: &[&str] = &["2", "3", "5.1"];
const LEDGER: &str = include_str!("pem.ledger");

fn every_must_of_rfc_7468_sections_2_3_and_5_1_is_in_the_ledger_of_pem() {
    let rfc = standard::read("rfc7468.txt");
    let tests: Vec<&str> = crate::once_tests()
        .iter()
        .chain(crate::machine_tests().iter())
        .map(|(name, _)| *name)
        .collect();
    if let Err(errors) = standard::check_ledger(&rfc, SECTIONS, LEDGER, &tests) {
        panic!("{}", errors.join("\n"));
    }
}
