//! An operation of an effect is private to its module unless `pub` is written
//! before it. Another module imports a `pub effect`, lists it on its functions
//! and runs under a handler the declaring module gives, and it can call or
//! handle only operations that are `pub`. Where an error is expected, the
//! generator that wrote the program says what it marked.

use smith::diag::Error;
use smith::lower_project;
use smith::scalar;

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "the_ledger_of_the_page_is_called_and_handled_by_its_own_module_and_used_by_another",
        the_ledger_of_the_page_is_called_and_handled_by_its_own_module_and_used_by_another,
    ),
    (
        "a_call_an_import_and_a_handler_of_a_private_operation_from_another_module_are_one_error_each",
        a_call_an_import_and_a_handler_of_a_private_operation_from_another_module_are_one_error_each,
    ),
    (
        "an_effect_with_a_pub_operation_and_a_private_one_cannot_be_handled_outside_and_its_pub_one_is_called",
        an_effect_with_a_pub_operation_and_a_private_one_cannot_be_handled_outside_and_its_pub_one_is_called,
    ),
    (
        "an_import_is_refused_exactly_for_the_operations_not_marked_pub_and_a_handler_when_any_is_not",
        an_import_is_refused_exactly_for_the_operations_not_marked_pub_and_a_handler_when_any_is_not,
    ),
];

fn errors(source: &str, modules: &[(&str, &str)]) -> Vec<Error> {
    lower_project(source.as_bytes(), modules)
        .err()
        .unwrap_or_default()
}

/// The module of the page: `record` is the module's own, and `pay` and `audit`
/// are what other modules use.
const LEDGER: &str = "use std.console (console, print)\n\n\
    pub effect ledger {\n  fn record(line: str)\n}\n\n\
    pub fn pay(amount: Nat) ! ledger, alloc {\n  record(\"paid {amount}\")\n}\n\n\
    pub fn audit[T](body: fn() -> T ! ledger) -> T ! console {\n  \
    ledger(record: fn(line) { print(line) }, body: body)\n}\n";

/// Another module imports `ledger`, writes `! ledger`, calls `pay`, and runs
/// under `audit`: what is recorded is what the functions of the ledger record.
fn the_ledger_of_the_page_is_called_and_handled_by_its_own_module_and_used_by_another() {
    let source = "use std.console (console)\nuse app.ledger (ledger, pay, audit)\n\n\
        fn twice() ! ledger, alloc {\n  pay(1)\n  pay(2)\n}\n\n\
        fn main() ! console, alloc {\n  audit() {\n    twice()\n  }\n}\n";
    let found = errors(source, &[("app.ledger", LEDGER)]);
    assert!(found.is_empty(), "{found:?}");
    let program = lower_project(source.as_bytes(), &[("app.ledger", LEDGER)]).expect("builds");
    assert_eq!(scalar::run(&program).stdout, b"paid 1\npaid 2\n");
}

/// Importing `record`, calling it with no import, and handling `ledger` in
/// another module are each refused, once, at the place that names it.
fn a_call_an_import_and_a_handler_of_a_private_operation_from_another_module_are_one_error_each() {
    let modules = [("app.ledger", LEDGER)];
    let imported = "use std.console (console)\nuse app.ledger (ledger, record)\n\n\
        fn main() ! console {\n}\n";
    let found = errors(imported, &modules);
    assert_eq!(found.len(), 1, "{found:?}");
    assert_eq!(found[0].message, "`record` is private to `app.ledger`");
    assert_eq!(found[0].pos.line, 2, "{found:?}");
    assert_eq!(
        found[0].fix.as_deref(),
        Some("write `pub` before `record` in `app/ledger.smith`")
    );

    let called = "use std.console (console)\nuse app.ledger (ledger, audit)\n\n\
        fn note() ! ledger, alloc {\n  record(\"x\")\n}\n\n\
        fn main() ! console, alloc {\n  audit() {\n    note()\n  }\n}\n";
    let found = errors(called, &modules);
    assert_eq!(found.len(), 1, "{found:?}");
    assert_eq!(found[0].pos.line, 5, "{found:?}");

    let handled = "use std.console (console)\nuse app.ledger (ledger)\n\n\
        fn quiet[T](body: fn() -> T ! ledger) -> T {\n  \
        ledger(record: fn(line) { }, body: body)\n}\n\n\
        fn main() ! console {\n  quiet() { }\n}\n";
    let found = errors(handled, &modules);
    assert_eq!(found.len(), 1, "{found:?}");
    assert_eq!(found[0].pos.line, 5, "{found:?}");
    assert_eq!(
        found[0].message,
        "`ledger` has operations that are private to `app.ledger`, so it cannot be handled here: `record`"
    );
    assert_eq!(
        found[0].fix.as_deref(),
        Some("write `pub` before `record` in `app/ledger.smith`")
    );
}

/// `shown` is `pub` and `hidden` is not: another module calls what carries the
/// effect and `shown` itself, and cannot handle the effect.
fn an_effect_with_a_pub_operation_and_a_private_one_cannot_be_handled_outside_and_its_pub_one_is_called()
 {
    let module = "pub effect two {\n  pub fn shown()\n  fn hidden()\n}\n\n\
        pub fn handled[T](body: fn() -> T ! two) -> T {\n  \
        two(shown: { }, hidden: { }, body: body)\n}\n\n\
        pub fn both() ! two {\n  shown()\n  hidden()\n}\n";
    let modules = [("app.two", module)];
    let calling = "use app.two (two, shown, handled, both)\n\n\
        fn main() {\n  handled() {\n    shown()\n    both()\n  }\n}\n";
    let found = errors(calling, &modules);
    assert!(found.is_empty(), "{found:?}");
    let handling = "use app.two (two, shown)\n\n\
        fn main() {\n  two(shown: { }, body: { })\n}\n";
    let found = errors(handling, &modules);
    assert_eq!(found.len(), 1, "{found:?}");
    assert_eq!(found[0].pos.line, 4, "{found:?}");
    assert!(
        found[0].message.ends_with("so it cannot be handled here: `hidden`"),
        "{found:?}"
    );
}

/// An effect with operations marked `pub` or not, as the generator says.
fn marks(g: &mut Gen) -> (Vec<bool>, Vec<bool>) {
    let count = g.below(3) + 1;
    let marked: Vec<bool> = (0..count).map(|_| g.below(2) == 0).collect();
    let imported: Vec<bool> = (0..count).map(|_| g.below(2) == 0).collect();
    (marked, imported)
}

fn effect_module(marked: &[bool]) -> String {
    let mut text = String::from("pub effect fx {\n");
    for (i, public) in marked.iter().enumerate() {
        text += &format!("  {}fn op{i}()\n", if *public { "pub " } else { "" });
    }
    text + "}\n"
}

/// A `use` lists the effect and some of its operations: each that is not
/// marked `pub` is refused at its place in the list, once, and the rest are
/// not; and a handler of the effect in another module is refused exactly when
/// some operation is private, once.
fn an_import_is_refused_exactly_for_the_operations_not_marked_pub_and_a_handler_when_any_is_not() {
    generate::check(7801, 40, marks, |(marked, imported)| {
        let module = effect_module(marked);
        let modules = [("app.fx", module.as_str())];
        let names: Vec<String> = (0..marked.len())
            .filter(|i| imported[*i])
            .map(|i| format!("op{i}"))
            .collect();
        let mut list = vec!["fx".to_string()];
        list.extend(names.clone());
        let source = format!("use app.fx ({})\n\nfn main() {{\n}}\n", list.join(", "));
        let found = errors(&source, &modules);
        let private: Vec<&String> = names
            .iter()
            .filter(|n| !marked[n[2..].parse::<usize>().unwrap()])
            .collect();
        assert_eq!(found.len(), private.len(), "{found:?} for\n{module}\n{source}");
        for error in &found {
            assert!(
                private.iter().any(|n| error.message == format!("`{n}` is private to `app.fx`")),
                "{found:?} for\n{module}"
            );
            assert_eq!(error.pos.line, 1);
        }

        let given: Vec<String> = (0..marked.len()).map(|i| format!("op{i}: {{ }}")).collect();
        let handler = format!(
            "use app.fx (fx)\n\nfn main() {{\n  fx({}, body: {{ }})\n}}\n",
            given.join(", ")
        );
        let found = errors(&handler, &modules);
        let hidden: Vec<String> = (0..marked.len())
            .filter(|i| !marked[*i])
            .map(|i| format!("`op{i}`"))
            .collect();
        if hidden.is_empty() {
            assert!(found.is_empty(), "{found:?} for\n{module}\n{handler}");
        } else {
            assert_eq!(found.len(), 1, "{found:?} for\n{module}\n{handler}");
            assert_eq!(found[0].pos.line, 4);
            assert!(
                found[0].message.ends_with(&format!("handled here: {}", hidden.join(", "))),
                "{found:?}"
            );
        }
    });
}
