//! `routes` and `get` and the other methods answer each request as `http.md`
//! says, under a handler of `http` that the test supplies: each parameter of a
//! function is read from where the page says, what the function gives is the
//! response, a request that does not fit the parameters is answered 400, and
//! one that fits no route is answered 404. The expectation of each request is
//! what the generator that wrote it computed by the rule.

use smith::scalar::{self, Outcome};
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::targets;

pub const ONCE: &[Test] = &[
    (
        "the_first_program_of_the_page_answers_a_user_a_missing_user_a_bad_id_and_a_path_with_no_route",
        the_first_program_of_the_page_answers_a_user_a_missing_user_a_bad_id_and_a_path_with_no_route,
    ),
    (
        "each_request_is_answered_as_the_route_it_fits_reads_and_answers_it",
        each_request_is_answered_as_the_route_it_fits_reads_and_answers_it,
    ),
];

/// The executables of the programs are checked against the evaluator.
pub const MACHINE: &[Test] = &[(
    "every_executable_answers_each_request_as_the_evaluator_does",
    every_executable_answers_each_request_as_the_evaluator_does,
)];

fn outcome(source: &str) -> Outcome {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
    scalar::run(&program)
}

fn printed(source: &str) -> String {
    String::from_utf8(outcome(source).stdout).expect("text")
}

/// The routes of the program, a handler of `http` that gives one request and
/// prints its response, and a handler of `store`.
const PROGRAM: &str = "use std.bits (u8, u64)
use std.console (console, print)
use std.http (NotFound, Request, Response, delete, get, http, post, routes, routing)

struct User {
  id: u64
  name: str
}

struct NewUser {
  name: str
}

struct Search {
  text: str
  limit: u8
}

effect store {
  fn find(id: u64) -> Option[User]
}

fn fixed_store[T](body: fn() -> T ! store) -> T {
  store(find: fn(id) { if id == 7 { Option.some(User(id: 7, name: \"Ana\")) } else { Option.none } }, body: body)
}

fn user(id: u64) -> Result[User, NotFound] ! store {
  match find(id) {
    .some(u) { Result.ok(u) }
    .none { Result.err(NotFound(message: \"no user\")) }
  }
}

fn create(new: NewUser) -> User {
  User(id: new.name.size, name: new.name)
}

fn remove(id: u64) { }

fn search(text: str, limit: u8 = 20) -> Search {
  Search(text: text, limit: limit)
}

fn copy_of(r: Request) -> Request ! alloc {
  Request(method: r.method, path: r.path, query: r.query, headers: [], body: r.body)
}

fn once[T](asked: Request, sink: fn(Response), body: fn() -> T ! http) -> T ! alloc {
  http(request: { copy_of(asked) }, send: fn(r) { sink(r) }, body: body)
}

fn ask(method: str, path: str, query: str, body: str) ! console, alloc {
  with once(Request(method: method, path: path, query: query, headers: [], body: body), fn(r) { print(\"{r.status} {r.body}\") })
  with fixed_store()
  with routes()
  get(\"/users/:id\", user)
  post(\"/users\", create)
  delete(\"/users/:id\", remove)
  get(\"/search\", search)
}

";

fn the_first_program_of_the_page_answers_a_user_a_missing_user_a_bad_id_and_a_path_with_no_route() {
    let source = format!(
        "{PROGRAM}fn main() ! console, alloc {{\n  ask(\"GET\", \"/users/7\", \"\", \"\")\n  ask(\"GET\", \"/users/8\", \"\", \"\")\n  ask(\"GET\", \"/users/x\", \"\", \"\")\n  ask(\"GET\", \"/nothing\", \"\", \"\")\n}}\n"
    );
    assert!(check(source.as_bytes()).is_ok(), "{:?}", check(source.as_bytes()).err());
    let said = printed(&source);
    let lines: Vec<&str> = said.lines().collect();
    assert_eq!(lines[0], "200 {\"id\":7,\"name\":\"Ana\"}");
    assert_eq!(lines[1], "404 {\"message\":\"no user\"}");
    assert!(lines[2].starts_with("400 {\"path\":\"id\",\"problem\":"), "{}", lines[2]);
    assert_eq!(lines[3], "404 ");
    assert_eq!(lines.len(), 4);
}

/// What a response is expected to be: its status, and its body as it is or as
/// far as the part that the rule gives.
#[derive(Clone, Debug)]
enum Body {
    Exact(String),
    Starts(String),
}

#[derive(Clone, Debug)]
struct Ask {
    method: &'static str,
    path: &'static str,
    query: &'static str,
    body: &'static str,
}

const METHODS: &[&str] = &["GET", "POST", "DELETE", "PUT"];
const PATHS: &[&str] = &[
    "/users/7", "/users/8", "/users/x", "/users/", "/users", "/search", "/nothing", "/users/7/x", "/users/99999999999999999999",
];
const QUERIES: &[&str] = &[
    "", "text=a", "text=a&limit=5", "text=a&limit=255", "text=a&limit=256", "text=a&limit=x", "limit=5", "text=", "text=ab&limit=0&other=1",
];
const BODIES: &[&str] = &["", "{\"name\":\"Bo\"}", "{\"name\":3}", "{\"name\":\"Bo\"} x", "{\"name\":\"Cyn\"}"];

fn ask(g: &mut Gen) -> Ask {
    Ask {
        method: g.pick(METHODS),
        path: g.pick(PATHS),
        query: g.pick(QUERIES),
        body: g.pick(BODIES),
    }
}

fn whole(text: &str) -> bool {
    let digits = text.strip_prefix('-').unwrap_or(text);
    !digits.is_empty() && digits.bytes().all(|b| b.is_ascii_digit())
}

fn member<'a>(query: &'a str, name: &str) -> Option<&'a str> {
    query
        .split('&')
        .filter(|p| !p.is_empty())
        .map(|p| p.split_once('=').unwrap_or((p, "")))
        .find(|(n, _)| *n == name)
        .map(|(_, v)| v)
}

fn bad(path: &str) -> (u16, Body) {
    (400, Body::Starts(format!("{{\"path\":\"{path}\",\"problem\":")))
}

/// The response of the routes, in their order, for the request: the first
/// route whose method and path fit answers it.
fn expected(a: &Ask) -> (u16, Body) {
    let parts: Vec<&str> = a.path.split('/').skip(1).collect();
    let by_id = |id: &str| -> Option<Result<u64, ()>> {
        if whole(id) { Some(id.parse::<u64>().map_err(|_| ())) } else { None }
    };
    match (a.method, parts.as_slice()) {
        ("GET", ["users", id]) => match by_id(id) {
            Some(Ok(7)) => (200, Body::Exact("{\"id\":7,\"name\":\"Ana\"}".into())),
            Some(Ok(_)) => (404, Body::Exact("{\"message\":\"no user\"}".into())),
            _ => bad("id"),
        },
        ("POST", ["users"]) => match a.body {
            "" => (400, Body::Exact("{\"path\":\"\",\"problem\":{\"missing\":\"new\"}}".into())),
            "{\"name\":\"Bo\"}" => (200, Body::Exact("{\"id\":2,\"name\":\"Bo\"}".into())),
            "{\"name\":\"Cyn\"}" => (200, Body::Exact("{\"id\":3,\"name\":\"Cyn\"}".into())),
            "{\"name\":3}" => bad("name"),
            _ => (400, Body::Starts("{\"path\":".into())),
        },
        ("DELETE", ["users", id]) => match by_id(id) {
            Some(Ok(_)) => (204, Body::Exact(String::new())),
            _ => bad("id"),
        },
        ("GET", ["search"]) => {
            let limit = match member(a.query, "limit") {
                None => Ok(20),
                Some(v) if whole(v) && !v.starts_with('-') => v.parse::<u8>().map_err(|_| ()),
                Some(_) => Err(()),
            };
            match (limit, member(a.query, "text")) {
                (Err(()), _) => bad("limit"),
                (Ok(_), None) => (400, Body::Exact("{\"path\":\"\",\"problem\":{\"missing\":\"text\"}}".into())),
                (Ok(n), Some(t)) => (200, Body::Exact(format!("{{\"text\":\"{t}\",\"limit\":{n}}}"))),
            }
        }
        _ => (404, Body::Exact(String::new())),
    }
}

fn quoted(s: &str) -> String {
    s.replace('\\', "\\\\").replace('"', "\\\"").replace('{', "\\{").replace('}', "\\}")
}

fn call(a: &Ask) -> String {
    format!(
        "  ask(\"{}\", \"{}\", \"{}\", \"{}\")\n",
        a.method,
        a.path,
        a.query,
        quoted(a.body)
    )
}

fn holds(line: &str, status: u16, body: &Body) -> bool {
    let want = format!("{status} ");
    match line.strip_prefix(&want) {
        None => false,
        Some(rest) => match body {
            Body::Exact(b) => rest == b,
            Body::Starts(p) => rest.starts_with(p.as_str()),
        },
    }
}

fn each_request_is_answered_as_the_route_it_fits_reads_and_answers_it() {
    let asks = |g: &mut Gen| (0..8).map(|_| ask(g)).collect::<Vec<Ask>>();
    generate::check(8701, 2, asks, |all| {
        let mut source = format!("{PROGRAM}fn main() ! console, alloc {{\n");
        for a in all {
            source += &call(a);
        }
        source += "}\n";
        let said = printed(&source);
        let lines: Vec<&str> = said.lines().collect();
        assert_eq!(lines.len(), all.len(), "for\n{source}\n{said}");
        for (a, line) in all.iter().zip(&lines) {
            let (status, body) = expected(a);
            assert!(holds(line, status, &body), "{a:?} gave {line:?}, wanted {status} {body:?}\nfor\n{source}");
        }
    });
}

fn every_executable_answers_each_request_as_the_evaluator_does() {
    let asks = [
        Ask { method: "GET", path: "/users/7", query: "", body: "" },
        Ask { method: "POST", path: "/users", query: "", body: "{\"name\":\"Bo\"}" },
        Ask { method: "GET", path: "/search", query: "text=a&limit=5", body: "" },
        Ask { method: "DELETE", path: "/users/7", query: "", body: "" },
        Ask { method: "GET", path: "/nothing", query: "", body: "" },
    ];
    let mut source = format!("{PROGRAM}fn main() ! console, alloc {{\n");
    for a in &asks {
        source += &call(a);
    }
    source += "}\n";
    let said = outcome(&source);
    assert_eq!(
        String::from_utf8(said.stdout.clone()).unwrap(),
        "200 {\"id\":7,\"name\":\"Ana\"}\n200 {\"id\":2,\"name\":\"Bo\"}\n200 {\"text\":\"a\",\"limit\":5}\n204 \n404 \n"
    );
    for target in targets::runnable() {
        targets::runs_as(source.as_bytes(), target, &said);
    }
}
