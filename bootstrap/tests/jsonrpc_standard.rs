//! `std.jsonrpc` is held to the text of the JSON-RPC 2.0 specification, which is
//! in `reference/jsonrpc/`: every sentence of it that holds MUST, MUST NOT, or
//! SHALL is in a line of the ledger, `jsonrpc.ledger`, with the test that holds
//! it or the reason it binds nothing the module does; and the helper is tried on
//! a ledger that has lost a line.

use crate::Test;
use crate::standard;

pub const ONCE: &[Test] = &[
    (
        "every_must_of_the_jsonrpc_specification_is_in_the_ledger_of_jsonrpc",
        every_must_of_the_jsonrpc_specification_is_in_the_ledger_of_jsonrpc,
    ),
    (
        "a_jsonrpc_ledger_that_lost_a_line_or_names_a_test_that_is_not_there_is_refused",
        a_jsonrpc_ledger_that_lost_a_line_or_names_a_test_that_is_not_there_is_refused,
    ),
];

const SECTIONS: &[&str] = &["1", "2", "3", "4", "5", "6", "7", "8"];
const LEDGER: &str = include_str!("jsonrpc.ledger");

fn specification() -> String {
    std::fs::read_to_string(concat!(env!("CARGO_MANIFEST_DIR"), "/../reference/jsonrpc/specification.html"))
        .expect("the specification is read by the tests")
}

fn test_names() -> Vec<&'static str> {
    crate::once_tests()
        .iter()
        .chain(crate::machine_tests().iter())
        .map(|(name, _)| *name)
        .collect()
}

fn every_must_of_the_jsonrpc_specification_is_in_the_ledger_of_jsonrpc() {
    let html = specification();
    if let Err(errors) = standard::check_ledger(&html, SECTIONS, LEDGER, &test_names()) {
        panic!("{}", errors.join("\n"));
    }
}

fn a_jsonrpc_ledger_that_lost_a_line_or_names_a_test_that_is_not_there_is_refused() {
    let html = specification();
    let tests = test_names();
    let sentences = standard::sentences(&html, SECTIONS);
    assert!(sentences.len() > 15, "the sections hold few sentences: {}", sentences.len());
    let lines: Vec<&str> = LEDGER.lines().filter(|l| !l.starts_with('#')).collect();
    for lost in 0..lines.len() {
        let ledger: String = lines
            .iter()
            .enumerate()
            .filter(|(i, _)| *i != lost)
            .map(|(_, l)| format!("{l}\n"))
            .collect();
        let errors = standard::check_ledger(&html, SECTIONS, &ledger, &tests).unwrap_err();
        assert_eq!(errors.len(), 1, "{errors:?}");
        assert!(errors[0].starts_with("in no line of the ledger"), "{errors:?}");
    }
    let wrong = LEDGER.replacen("| test a_", "| test not_a_", 1);
    let errors = standard::check_ledger(&html, SECTIONS, &wrong, &tests).unwrap_err();
    assert!(errors.iter().any(|e| e.contains("does not exist")), "{errors:?}");
}
