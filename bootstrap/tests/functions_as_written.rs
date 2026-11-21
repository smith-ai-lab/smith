//! A function is found by the module that wrote its name, as the module wrote
//! it, and not by a name the tokens were rewritten to. A name a file imports
//! means that name in that file, and a name that a parameter holds is the
//! parameter wherever a module has a function of that name.

use smith::{check, lower_project};

use crate::Test;

pub const ONCE: &[Test] = &[
    (
        "a_name_listed_from_one_library_module_is_no_other_modules_name",
        a_name_listed_from_one_library_module_is_no_other_modules_name,
    ),
    (
        "a_parameter_named_as_a_function_of_its_module_calls_for_nothing",
        a_parameter_named_as_a_function_of_its_module_calls_for_nothing,
    ),
    (
        "an_operation_with_a_type_the_file_does_not_name_is_refused_as_a_value",
        an_operation_with_a_type_the_file_does_not_name_is_refused_as_a_value,
    ),
    (
        "an_effect_a_file_does_not_list_is_no_effect_of_the_file",
        an_effect_a_file_does_not_list_is_no_effect_of_the_file,
    ),
];

fn errors(source: &str) -> Vec<String> {
    match check(source.as_bytes()) {
        Ok(_) => Vec::new(),
        Err(errors) => errors.into_iter().map(|e| e.message).collect(),
    }
}

/// Each program lists a name that another module of the library defines or
/// lists, and uses it, with the module that has the other one, which a file
/// that writes names as they are once confused.
fn a_name_listed_from_one_library_module_is_no_other_modules_name() {
    let programs = [
        "use std.json (Cases, to_json)\nuse std.html (Html, render, text)\n\nfn show(h: Html) {\n}\n\nfn main() ! alloc {\n  show(render() {\n    text(to_json(1))\n  })\n}\n",
        "use std.html (Html, render)\n\nfn f(s: Html) {\n}\n\nfn main() ! alloc {\n  f(render() { })\n}\n",
        "use std.mcp (Incoming)\nuse std.jsonrpc (read)\n\nfn f(i: Incoming) {\n}\n\nfn main() ! alloc {\n  match read(\"1\") {\n    .ok(r) { }\n    .err(e) { }\n  }\n}\n",
        "use std.text (text, finish)\nuse std.mcp (Incoming)\n\nfn f(i: Incoming) {\n}\n\nfn main() ! alloc {\n  t = finish(text())\n}\n",
    ];
    for source in programs {
        let found = errors(source);
        assert!(found.is_empty(), "{found:?} for\n{source}");
    }
}

/// A function of a module that is reached only by a name that a parameter of
/// another function of the module holds is not reached: its mistake is not
/// reported.
fn a_parameter_named_as_a_function_of_its_module_calls_for_nothing() {
    let module = "pub fn pick(first: Int, second: Int) -> Int {\n  second\n}\n\n\
                  pub fn second() -> Int {\n  1 / 0\n}\n";
    let source = "use app.m (pick)\n\nfn main() {\n  x = pick(1, 2)\n}\n";
    let built = lower_project(source.as_bytes(), &[("app.m", module)]);
    assert!(built.is_ok(), "{:?}", built.err());
}

/// An effect that a module declares is its own name, and a file that lists
/// the operation but not the effect cannot write the effect.
fn an_effect_a_file_does_not_list_is_no_effect_of_the_file() {
    let module = "pub effect fx {\n  pub fn op0()\n}\n";
    let source = "use app.fx (op0)\n\nfn f() ! fx {\n  op0()\n}\n\nfn main() ! fx {\n  f()\n}\n";
    let built = lower_project(source.as_bytes(), &[("app.fx", module)]);
    let Err(found) = built else {
        panic!("accepted");
    };
    assert!(
        found.iter().any(|e| e.message.contains("no effect named `fx`")),
        "{found:?}"
    );
}

/// `print` has a parameter of `Stream`, which the file does not import, so its
/// name as a value had a type that passed for any type, and what the program
/// built from it was no program: the mistake is reported where the name is.
fn an_operation_with_a_type_the_file_does_not_name_is_refused_as_a_value() {
    let source = "use std.console (console, print)\n\nfn main() ! console {\n  x = print\n  print(x)\n}\n";
    let found = errors(source);
    assert!(
        found.len() == 1 && found[0].contains("passing a function with a type this file does not name"),
        "{found:?}"
    );
}
