//! `std.http` reads the parts of a request that a route's parameters come
//! from: the parts of a path that a pattern with `:name` parts fits, and the
//! members of a query. The expectation of each program is what the generator
//! that wrote its patterns, paths, and queries computed for each by the rule.

use smith::scalar::{self, Outcome};
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::targets;

pub const ONCE: &[Test] = &[
    (
        "a_pattern_fits_the_path_with_the_same_parts_and_gives_each_name_its_part",
        a_pattern_fits_the_path_with_the_same_parts_and_gives_each_name_its_part,
    ),
    (
        "a_query_is_its_members_between_ampersands_with_the_value_after_the_first_equals",
        a_query_is_its_members_between_ampersands_with_the_value_after_the_first_equals,
    ),
    (
        "an_error_of_the_library_has_the_status_its_name_says",
        an_error_of_the_library_has_the_status_its_name_says,
    ),
];

/// The executables of the programs are checked against the evaluator.
pub const MACHINE: &[Test] = &[(
    "every_executable_reads_the_parts_of_a_request_as_the_evaluator_does",
    every_executable_reads_the_parts_of_a_request_as_the_evaluator_does,
)];

fn outcome(source: &str) -> Outcome {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
    scalar::run(&program)
}

fn printed(source: &str) -> String {
    String::from_utf8(outcome(source).stdout).expect("text")
}

const USES: &str = "use std.console (console, print)\nuse std.http (Part, query_parts, route_match)\n\n";

/// Prints each part of a list as `name=value`.
const SHOW: &str = "fn show(parts: List[Part]) ! console, alloc {\n  loop i in 0..parts.size {\n    print(\"{parts[i].name}={parts[i].value}\")\n  }\n}\n\n";

const WORDS: &[&str] = &["users", "notes", "a", "x1", "items", "7", "42", "ana"];
const NAMES: &[&str] = &["id", "name", "n", "slug"];

/// A pattern, as its parts, and the path that it is asked about.
#[derive(Clone, Debug)]
struct Route {
    pattern: Vec<String>,
    path: Vec<String>,
}

fn route(g: &mut Gen) -> Route {
    let count = g.below(4);
    let pattern: Vec<String> = (0..count)
        .map(|k| {
            if g.one_in(2) {
                format!(":{}{k}", g.pick(NAMES))
            } else {
                g.pick(WORDS).to_string()
            }
        })
        .collect();
    // A path that fits, then one change for some.
    let mut path: Vec<String> = pattern
        .iter()
        .map(|p| {
            if p.starts_with(':') {
                g.pick(WORDS).to_string()
            } else {
                p.clone()
            }
        })
        .collect();
    match g.below(4) {
        0 if !path.is_empty() => {
            let at = g.below(path.len());
            path[at] = "other".to_string();
        }
        1 => path.push("extra".to_string()),
        2 if !path.is_empty() => {
            path.pop();
        }
        _ => {}
    }
    Route { pattern, path }
}

fn joined(parts: &[String]) -> String {
    if parts.is_empty() {
        "/".to_string()
    } else {
        format!("/{}", parts.join("/"))
    }
}

/// What `route_match` gives for the route: nothing, or each name and part.
fn matched(r: &Route) -> Option<Vec<(String, String)>> {
    if r.pattern.len() != r.path.len() {
        return None;
    }
    let mut found = Vec::new();
    for (p, q) in r.pattern.iter().zip(&r.path) {
        match p.strip_prefix(':') {
            Some(name) if !name.is_empty() => found.push((name.to_string(), q.clone())),
            _ if p == q => {}
            _ => return None,
        }
    }
    Some(found)
}

fn a_pattern_fits_the_path_with_the_same_parts_and_gives_each_name_its_part() {
    let routes = |g: &mut Gen| (0..6).map(|_| route(g)).collect::<Vec<Route>>();
    generate::check(8201, 10, routes, |all| {
        let mut source = format!("{USES}{SHOW}fn main() ! console, alloc {{\n");
        let mut said = String::new();
        for r in all {
            source += &format!(
                "  match route_match(\"{}\", \"{}\") {{\n    .some(parts) {{\n      print(\"fits\")\n      show(parts)\n    }}\n    .none {{ print(\"no\") }}\n  }}\n",
                joined(&r.pattern),
                joined(&r.path)
            );
            match matched(r) {
                Some(parts) => {
                    said += "fits\n";
                    for (n, v) in parts {
                        said += &format!("{n}={v}\n");
                    }
                }
                None => said += "no\n",
            }
        }
        source += "}\n";
        assert_eq!(printed(&source), said, "for\n{source}");
    });
}

/// A query of generated pieces, each with a name, perhaps an `=`, and a value.
fn query(g: &mut Gen) -> String {
    let pieces: Vec<String> = (0..g.below(5))
        .map(|_| match g.below(5) {
            0 => String::new(),
            1 => g.pick(NAMES).to_string(),
            2 => format!("{}=", g.pick(NAMES)),
            3 => format!("{}={}", g.pick(NAMES), g.pick(WORDS)),
            _ => format!("{}={}={}", g.pick(NAMES), g.pick(WORDS), g.pick(WORDS)),
        })
        .collect();
    pieces.join("&")
}

fn members(query: &str) -> Vec<(String, String)> {
    query
        .split('&')
        .filter(|p| !p.is_empty())
        .map(|p| match p.split_once('=') {
            Some((n, v)) => (n.to_string(), v.to_string()),
            None => (p.to_string(), String::new()),
        })
        .collect()
}

fn a_query_is_its_members_between_ampersands_with_the_value_after_the_first_equals() {
    let queries = |g: &mut Gen| (0..6).map(|_| query(g)).collect::<Vec<String>>();
    generate::check(8202, 10, queries, |all| {
        let mut source = format!("{USES}{SHOW}fn main() ! console, alloc {{\n");
        let mut said = String::new();
        for q in all {
            source += &format!("  print(\"--\")\n  show(query_parts(\"{q}\"))\n");
            said += "--\n";
            for (n, v) in members(q) {
                said += &format!("{n}={v}\n");
            }
        }
        source += "}\n";
        assert_eq!(printed(&source), said, "for\n{source}");
    });
}

fn an_error_of_the_library_has_the_status_its_name_says() {
    let source = "use std.console (console, print)\nuse std.http (BadRequest, NotFound, Unauthorized, status)\n\n\
fn main() ! console, alloc {\n  print(\"{status(NotFound(message: \"a\"))}\")\n  \
print(\"{status(BadRequest(message: \"b\"))}\")\n  print(\"{status(Unauthorized(message: \"c\"))}\")\n}\n";
    assert!(check(source.as_bytes()).is_ok(), "{:?}", check(source.as_bytes()).err());
    assert_eq!(printed(source), "404\n400\n401\n");
}

fn every_executable_reads_the_parts_of_a_request_as_the_evaluator_does() {
    let source = format!(
        "{USES}{SHOW}fn main() ! console, alloc {{\n  \
         match route_match(\"/users/:id/notes/:n\", \"/users/7/notes/x\") {{\n    .some(parts) {{ show(parts) }}\n    .none {{ print(\"no\") }}\n  }}\n  \
         show(query_parts(\"a=1&b=&c&&d=x=y\"))\n}}\n"
    );
    let said = outcome(&source);
    assert_eq!(
        String::from_utf8(said.stdout.clone()).unwrap(),
        "id=7\nn=x\na=1\nb=\nc=\nd=x=y\n"
    );
    for target in targets::runnable() {
        targets::runs_as(source.as_bytes(), target, &said);
    }
}
