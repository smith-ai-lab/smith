//! `std.http` reads the parameters of a function that a route calls from the
//! parts of a request: a part of the path or of the query as the type of the
//! parameter, an `Option` or a parameter with a default left out when the
//! request has no part for it, and a parameter that is a struct from the
//! body as JSON. The expectation of each request is what the generator that
//! wrote it computed by the rule.

use smith::scalar::{self, Outcome};
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::targets;

pub const ONCE: &[Test] = &[
    (
        "the_parameters_of_a_function_are_read_from_the_path_the_query_and_the_body_as_their_types",
        the_parameters_of_a_function_are_read_from_the_path_the_query_and_the_body_as_their_types,
    ),
    (
        "the_names_of_the_parameters_are_told_in_order_with_the_ones_of_the_body_marked",
        the_names_of_the_parameters_are_told_in_order_with_the_ones_of_the_body_marked,
    ),
];

/// The executables of the programs are checked against the evaluator.
pub const MACHINE: &[Test] = &[(
    "every_executable_reads_the_parameters_of_a_request_as_the_evaluator_does",
    every_executable_reads_the_parameters_of_a_request_as_the_evaluator_does,
)];

fn outcome(source: &str) -> Outcome {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
    scalar::run(&program)
}

fn printed(source: &str) -> String {
    String::from_utf8(outcome(source).stdout).expect("text")
}

/// A function of a path part `id`, of the query parts `limit` and `size`, and
/// of the body `note`, which is run on a request given as its parts.
const PROGRAM: &str = "use std.bits (u64, u8)\nuse std.console (console, print)\nuse std.describe (Describe)\nuse std.deserialize (Problem, ReadError)\nuse std.http (FromRequest, Part, no_params, params_of, reading)\n\n\
struct Note {\n  text: str\n}\n\n\
struct Args {\n  id: u64\n  limit: Option[u8]\n  size: u8 = 20\n  note: Option[Note]\n}\n\n\
fn kind(p: Problem) -> str {\n  match p {\n    .missing(m) { \"missing\" }\n    .malformed { \"malformed\" }\n    .shape(s) { \"shape\" }\n    .range(n) { \"range\" }\n    .case(c) { \"case\" }\n    .unknown(u) { \"unknown\" }\n    .twice(t) { \"twice\" }\n  }\n}\n\n\
fn shown(n: Option[u8]) -> str ! alloc {\n  match n {\n    .some(v) { \"{v}\" }\n    .none { \"-\" }\n  }\n}\n\n\
fn run(path: take List[Part], query: take List[Part], body: str) ! console, alloc {\n  p = Describe[Args].describe(no_params())\n  r = reading(params_of(p), path, query, body)\n  got: Result[Args, ReadError] = FromRequest[Args].from_request(r)\n  match got {\n    .ok(a) {\n      text = match a.note {\n        .some(n) { n.text }\n        .none { \"-\" }\n      }\n      print(\"ok {a.id} {shown(a.limit)} {a.size} {text}\")\n    }\n    .err(e) { print(\"err {e.path} {kind(e.problem)}\") }\n  }\n}\n\n";

/// How a generated request spells each part, and what the rule says of it.
#[derive(Clone, Debug)]
struct Request {
    id: Option<&'static str>,
    limit: Option<&'static str>,
    size: Option<&'static str>,
    body: Option<&'static str>,
}

const IDS: &[&str] = &["7", "0", "x", "", "99999999999999999999", "-3"];
const NUMBERS: &[&str] = &["9", "255", "300", "x", "", "0"];
const BODIES: &[&str] = &[
    "{\"text\":\"hi\"}",
    "{\"text\":3}",
    "{\"text\":\"a\"} x",
    "{\"text\":\"b\"}  ",
];

fn request(g: &mut Gen) -> Request {
    let some = |g: &mut Gen, all: &'static [&'static str]| (g.below(3) > 0).then(|| g.pick(all)).copied();
    Request {
        id: some(g, IDS),
        limit: some(g, NUMBERS),
        size: some(g, NUMBERS),
        body: (g.below(2) == 0).then(|| *g.pick(BODIES)),
    }
}

/// What the rule says a whole number text of a `u8` parameter is read as: the
/// number, a text that is not one (`shape`), or one past the range (`range`).
fn byte(text: &str) -> Result<u8, &'static str> {
    if text.is_empty() || !text.bytes().all(|b| b.is_ascii_digit()) {
        return Err("shape");
    }
    text.parse::<u8>().map_err(|_| "range")
}

/// The line the program prints for the request, by the rule: the parameters
/// are read in order, `id`, `limit`, `size`, `note`, the first that cannot be
/// read is the error, and what is left out is then `missing` for `id` and is
/// as it is for the others.
fn said(r: &Request) -> String {
    let mut id = None;
    if let Some(text) = r.id {
        // A whole number is its digits with a `-` before them, and one that is
        // not in the range of the type is `range`, as `fromtext.md` says.
        let digits = text.strip_prefix('-').unwrap_or(text);
        let whole = !digits.is_empty() && digits.bytes().all(|b| b.is_ascii_digit());
        match text.parse::<u64>() {
            Ok(n) if whole => id = Some(n),
            _ if whole => return "err id range\n".into(),
            _ => return "err id shape\n".into(),
        }
    }
    let mut limit = None;
    if let Some(text) = r.limit {
        match byte(text) {
            Ok(n) => limit = Some(n),
            Err(kind) => return format!("err limit {kind}\n"),
        }
    }
    let mut size = 20;
    if let Some(text) = r.size {
        match byte(text) {
            Ok(n) => size = n,
            Err(kind) => return format!("err size {kind}\n"),
        }
    }
    let mut note = "-".to_string();
    let mut trailing = false;
    if let Some(body) = r.body {
        match body {
            "{\"text\":\"hi\"}" => note = "hi".into(),
            "{\"text\":\"b\"}  " => note = "b".into(),
            "{\"text\":3}" => return "err text shape\n".into(),
            // What follows the body is looked at when the parameters are all
            // read, so a parameter that is missing is said first.
            _ => trailing = true,
        }
    }
    match id {
        None => "err  missing\n".into(),
        Some(_) if trailing => "err  malformed\n".into(),
        Some(id) => {
            let limit = limit.map_or("-".to_string(), |n| n.to_string());
            format!("ok {id} {limit} {size} {note}\n")
        }
    }
}

fn parts(name: &str, value: Option<&str>) -> Option<String> {
    value.map(|v| format!("Part(name: \"{name}\", value: \"{v}\")"))
}

fn call(r: &Request) -> String {
    let path: Vec<String> = parts("id", r.id).into_iter().collect();
    let query: Vec<String> = [parts("limit", r.limit), parts("size", r.size)]
        .into_iter()
        .flatten()
        .collect();
    let body = r.body.unwrap_or("").replace('\\', "\\\\").replace('"', "\\\"").replace('{', "\\{").replace('}', "\\}");
    format!(
        "  run([{}], [{}], \"{body}\")\n",
        path.join(", "),
        query.join(", ")
    )
}

fn the_parameters_of_a_function_are_read_from_the_path_the_query_and_the_body_as_their_types() {
    let requests = |g: &mut Gen| (0..10).map(|_| request(g)).collect::<Vec<Request>>();
    generate::check(8301, 2, requests, |all| {
        let mut source = format!("{PROGRAM}fn main() ! console, alloc {{\n");
        let mut expected = String::new();
        for r in all {
            source += &call(r);
            expected += &said(r);
        }
        source += "}\n";
        assert_eq!(printed(&source), expected, "for\n{source}");
    });
}

fn the_names_of_the_parameters_are_told_in_order_with_the_ones_of_the_body_marked() {
    let source = "use std.bits (u64)\nuse std.console (console, print)\nuse std.describe (Describe)\nuse std.http (no_params, params_of)\n\n\
struct Note {\n  text: str\n}\n\n\
struct Args {\n  id: u64\n  words: Option[str]\n  flag: bool\n  note: Note\n  tags: List[str]\n}\n\n\
fn main() ! console, alloc {\n  p = Describe[Args].describe(no_params())\n  loop each in params_of(p) {\n    print(\"{each.name} {each.body}\")\n  }\n}\n";
    assert!(check(source.as_bytes()).is_ok(), "{:?}", check(source.as_bytes()).err());
    assert_eq!(
        printed(source),
        "id false\nwords false\nflag false\nnote true\ntags true\n"
    );
}

fn every_executable_reads_the_parameters_of_a_request_as_the_evaluator_does() {
    let requests = [
        Request { id: Some("7"), limit: Some("9"), size: None, body: Some("{\"text\":\"hi\"}") },
        Request { id: Some("7"), limit: Some("300"), size: None, body: None },
        Request { id: None, limit: None, size: None, body: None },
    ];
    let mut source = format!("{PROGRAM}fn main() ! console, alloc {{\n");
    for r in &requests {
        source += &call(r);
    }
    source += "}\n";
    let said = outcome(&source);
    assert_eq!(
        String::from_utf8(said.stdout.clone()).unwrap(),
        "ok 7 9 20 hi\nerr limit range\nerr  missing\n"
    );
    for target in targets::runnable() {
        targets::runs_as(source.as_bytes(), target, &said);
    }
}
