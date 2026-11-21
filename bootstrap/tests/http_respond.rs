//! What a function gives is the response, through `Respond`: a value with
//! `Serialize` as JSON with status 200, a page, a text, a redirect, and `()`
//! as their own `impl` says, a `Result` as its value or as its error with the
//! status of `Status`, and a type of a program's own with the `impl` it writes.

use smith::scalar::{self, Outcome};
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::targets;

pub const ONCE: &[Test] = &[
    (
        "a_value_with_serialize_is_answered_as_json_with_status_200",
        a_value_with_serialize_is_answered_as_json_with_status_200,
    ),
    (
        "a_result_is_answered_as_its_value_or_as_its_error_with_the_status_of_the_error",
        a_result_is_answered_as_its_value_or_as_its_error_with_the_status_of_the_error,
    ),
    (
        "a_type_that_writes_its_own_respond_is_answered_as_it_says_and_not_as_json",
        a_type_that_writes_its_own_respond_is_answered_as_it_says_and_not_as_json,
    ),
];

/// The executables of the programs are checked against the evaluator.
pub const MACHINE: &[Test] = &[(
    "every_executable_answers_each_kind_of_value_as_the_evaluator_does",
    every_executable_answers_each_kind_of_value_as_the_evaluator_does,
)];

fn outcome(source: &str) -> Outcome {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
    scalar::run(&program)
}

fn printed(source: &str) -> String {
    String::from_utf8(outcome(source).stdout).expect("text")
}

/// Prints each response as its status, its headers, and its body.
const SHOW: &str = "fn show(r: Response) ! console, alloc {\n  print(\"{r.status}\")\n  loop i in 0..r.headers.size {\n    (name, value) = r.headers[i]\n    print(\"{name}: {value}\")\n  }\n  print(r.body)\n}\n\n";

const NAMES: &[&str] = &["Ana", "a\\\"b", "x y", "", "caf\u{e9}", "back\\\\slash"];

fn json_text(name: &str) -> String {
    // The names above are written in the source with their escapes, which the
    // writer of JSON writes again: `"` as `\"`, and `\` as `\\`.
    let mut out = String::new();
    let mut chars = name.chars().peekable();
    while let Some(c) = chars.next() {
        if c == '\\' {
            let next = chars.next().expect("an escape");
            match next {
                '"' => out += "\\\"",
                '\\' => out += "\\\\",
                other => panic!("no escape {other}"),
            }
        } else {
            out.push(c);
        }
    }
    out
}

fn a_value_with_serialize_is_answered_as_json_with_status_200() {
    let users = |g: &mut Gen| {
        (0..4)
            .map(|_| (g.below(1000), *g.pick(NAMES)))
            .collect::<Vec<(usize, &str)>>()
    };
    generate::check(8501, 3, users, |all| {
        let mut source = format!(
            "use std.bits (u64)\nuse std.console (console, print)\nuse std.http (Respond, Response)\n\nstruct User {{\n  id: u64\n  name: str\n}}\n\n{SHOW}fn main() ! console, alloc {{\n"
        );
        let mut expected = String::new();
        for (id, name) in all {
            source += &format!(
                "  show(Respond[User].respond(User(id: {id}, name: \"{name}\")))\n"
            );
            expected += &format!(
                "200\ncontent-type: application/json\n{{\"id\":{id},\"name\":\"{}\"}}\n",
                json_text(name)
            );
        }
        source += "}\n";
        assert_eq!(printed(&source), expected, "for\n{source}");
    });
}

fn a_result_is_answered_as_its_value_or_as_its_error_with_the_status_of_the_error() {
    let source = format!(
        "use std.bits (u64)\nuse std.console (console, print)\nuse std.http (BadRequest, NotFound, Respond, Response, Unauthorized)\n\nstruct User {{\n  id: u64\n}}\n\n{SHOW}\
fn main() ! console, alloc {{\n  \
ok: Result[User, NotFound] = Result.ok(User(id: 7))\n  show(Respond[Result[User, NotFound]].respond(ok))\n  \
a: Result[User, NotFound] = Result.err(NotFound(message: \"no\"))\n  show(Respond[Result[User, NotFound]].respond(a))\n  \
b: Result[User, BadRequest] = Result.err(BadRequest(message: \"bad\"))\n  show(Respond[Result[User, BadRequest]].respond(b))\n  \
c: Result[User, Unauthorized] = Result.err(Unauthorized(message: \"who\"))\n  show(Respond[Result[User, Unauthorized]].respond(c))\n}}\n"
    );
    let json = "content-type: application/json\n";
    assert_eq!(
        printed(&source),
        format!(
            "200\n{json}{{\"id\":7}}\n404\n{json}{{\"message\":\"no\"}}\n400\n{json}{{\"message\":\"bad\"}}\n401\n{json}{{\"message\":\"who\"}}\n"
        )
    );
}

fn a_type_that_writes_its_own_respond_is_answered_as_it_says_and_not_as_json() {
    let source = format!(
        "use std.bits (u16)\nuse std.console (console, print)\nuse std.http (Respond, Response, Status)\n\nstruct Taken {{\n  name: str\n}}\n\nimpl Status[Taken] {{\n  fn status(e: Taken) -> u16 {{ 409 }}\n}}\n\n\
struct Picture {{\n  name: str\n}}\n\nimpl Respond[Picture] {{\n  fn respond(value: Picture) -> Response ! alloc {{\n    Response(status: 200, headers: [(\"content-type\", \"image/png\")], body: value.name)\n  }}\n}}\n\n{SHOW}\
fn send[T](x: T) -> Response ! alloc\n  requires Respond[T]\n{{\n  Respond[T].respond(x)\n}}\n\n\
fn main() ! console, alloc {{\n  show(send(Picture(name: \"cat\")))\n  \
t: Result[Picture, Taken] = Result.err(Taken(name: \"ana\"))\n  show(send(t))\n}}\n"
    );
    assert!(check(source.as_bytes()).is_ok(), "{:?}", check(source.as_bytes()).err());
    assert_eq!(
        printed(&source),
        "200\ncontent-type: image/png\ncat\n409\ncontent-type: application/json\n{\"name\":\"ana\"}\n"
    );
}

const EACH: &str = "use std.bits (u64)\nuse std.console (console, print)\nuse std.html (render, text)\nuse std.http (NotFound, Redirect, Respond, Response, Text)\n\nstruct User {\n  id: u64\n}\n\n";

fn every_executable_answers_each_kind_of_value_as_the_evaluator_does() {
    let source = format!(
        "{EACH}{SHOW}fn main() ! console, alloc {{\n  \
show(Respond[User].respond(User(id: 7)))\n  \
show(Respond[Text].respond(Text(text: \"hi\")))\n  \
show(Respond[Redirect].respond(Redirect(to: \"/login\")))\n  \
show(Respond[()].respond(()))\n  \
page = render() {{ text(\"a<b\") }}\n  show(Respond[Html].respond(page))\n  \
bad: Result[User, NotFound] = Result.err(NotFound(message: \"no\"))\n  show(Respond[Result[User, NotFound]].respond(bad))\n}}\n"
    )
    .replace("use std.html (render, text)", "use std.html (Html, render, text)");
    let said = outcome(&source);
    assert_eq!(
        String::from_utf8(said.stdout.clone()).unwrap(),
        "200\ncontent-type: application/json\n{\"id\":7}\n200\ncontent-type: text/plain; charset=utf-8\nhi\n302\nlocation: /login\n\n204\n\n200\ncontent-type: text/html; charset=utf-8\na&lt;b\n404\ncontent-type: application/json\n{\"message\":\"no\"}\n"
    );
    for target in targets::runnable() {
        targets::runs_as(source.as_bytes(), target, &said);
    }
}
