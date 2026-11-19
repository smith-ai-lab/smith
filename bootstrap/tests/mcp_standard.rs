//! The Model Context Protocol is held to the text of its tag 2026-07-28, which
//! is in `reference/mcp/2026-07-28/`: every sentence that holds MUST, MUST NOT,
//! or SHALL in the pages that a server is bound by is in a line of the ledger,
//! `mcp.ledger`, which says for now that what holds it is to be decided; every
//! page of the specification is one of those taken or one of those left out,
//! with a reason; the helper that reads the sentences and the ledger is tried on
//! a ledger that has lost a line, one that names a sentence twice, and one that
//! names a test that is not there; and each example of the schema that is a
//! message is read by `std.jsonrpc` as the kind of message the schema says it is.
//!
//! The programs name no type of `std.jsonrpc`: a name that a program imports
//! from it with a list leaks into the library modules it reaches, so they use
//! the module as `jsonrpc` and write what it gives in patterns.

use smith::{lower, scalar};

use crate::Test;
use crate::standard;

pub const ONCE: &[Test] = &[
    (
        "every_must_of_the_mcp_pages_a_server_is_bound_by_is_in_the_ledger_of_mcp",
        every_must_of_the_mcp_pages_a_server_is_bound_by_is_in_the_ledger_of_mcp,
    ),
    (
        "a_mcp_ledger_that_lost_a_line_or_names_a_sentence_twice_or_a_test_that_is_not_there_is_refused",
        a_mcp_ledger_that_lost_a_line_or_names_a_sentence_twice_or_a_test_that_is_not_there_is_refused,
    ),
    (
        "every_page_of_the_mcp_specification_is_taken_or_left_out_with_a_reason",
        every_page_of_the_mcp_specification_is_taken_or_left_out_with_a_reason,
    ),
    (
        "every_example_of_the_mcp_schema_that_is_a_message_is_read_as_the_message_it_is",
        every_example_of_the_mcp_schema_that_is_a_message_is_read_as_the_message_it_is,
    ),
];

const LEDGER: &str = include_str!("mcp.ledger");
const SPECIFICATION: &str = "mcp/2026-07-28/specification";

/// The pages that a server is bound by, as the path in the specification
/// without `.mdx`: what a server answers, what it checks of each request on
/// each transport, what it sends of itself, and what a server that protects
/// itself sends and checks.
const TAKEN: &[&str] = &[
    "basic/index",
    "basic/versioning",
    "basic/transports/index",
    "basic/transports/streamable-http",
    "basic/transports/stdio",
    "basic/patterns/index",
    "basic/patterns/subscriptions",
    "basic/patterns/cancellation",
    "basic/patterns/progress",
    "basic/patterns/mrtr",
    "basic/authorization/index",
    "basic/authorization/authorization-server-discovery",
    "basic/authorization/security-considerations",
    "server/index",
    "server/discover",
    "server/tools",
    "server/resources",
    "server/prompts",
    "server/utilities/caching",
    "server/utilities/pagination",
    "server/utilities/completion",
    "server/utilities/logging",
];

/// The pages that are not, each with why.
const LEFT_OUT: &[(&str, &str)] = &[
    (
        "index",
        "says what the key words mean; its one sentence with MUST is no requirement of a server",
    ),
    ("architecture/index", "describes the roles and holds none of the words of requirement"),
    (
        "changelog",
        "lists what changed since 2025-11-25; each of its sentences restates one of a page taken",
    ),
    ("deprecated", "lists the features that are deprecated and holds none of the words of requirement"),
    (
        "basic/authorization/client-registration",
        "binds a client that registers and an authorization server that takes the registration; a server that protects itself does neither",
    ),
    (
        "client/elicitation",
        "a feature of a client that a server of tools, resources, and prompts never asks for; the issue leaves it out",
    ),
    (
        "client/roots",
        "a feature of a client that a server of tools, resources, and prompts never asks for; the issue leaves it out",
    ),
    (
        "client/sampling",
        "a feature of a client that a server of tools, resources, and prompts never asks for; the issue leaves it out",
    ),
];

/// The names of every test that there is.
fn test_names() -> Vec<&'static str> {
    crate::once_tests()
        .iter()
        .chain(crate::machine_tests().iter())
        .map(|(name, _)| *name)
        .collect()
}

/// Every sentence of the pages taken, in the order of the pages.
fn sentences() -> Vec<standard::Sentence> {
    TAKEN
        .iter()
        .flat_map(|page| {
            let text = standard::read_under(SPECIFICATION, &format!("{page}.mdx"));
            standard::mdx_sentences(page, &text)
        })
        .collect()
}

fn every_must_of_the_mcp_pages_a_server_is_bound_by_is_in_the_ledger_of_mcp() {
    if let Err(errors) = standard::check_sentences(&sentences(), LEDGER, &test_names()) {
        panic!("{}", errors.join("\n"));
    }
}

fn a_mcp_ledger_that_lost_a_line_or_names_a_sentence_twice_or_a_test_that_is_not_there_is_refused() {
    let sentences = sentences();
    let tests = test_names();
    assert!(sentences.len() > 200, "the pages hold few sentences: {}", sentences.len());
    let lines: Vec<&str> = LEDGER.lines().filter(|l| !l.starts_with('#') && !l.is_empty()).collect();
    let join = |lines: &[&str]| lines.iter().map(|l| format!("{l}\n")).collect::<String>();
    // The ledger with a line away, for the first, the last, and every seventh
    // between: the sentence of that line is the one said to be in none.
    let mut lost: Vec<usize> = (0..lines.len()).step_by(7).collect();
    lost.push(lines.len() - 1);
    for lost in lost {
        let mut without = lines.clone();
        without.remove(lost);
        let errors = standard::check_sentences(&sentences, &join(&without), &tests).unwrap_err();
        assert_eq!(errors.len(), 1, "{errors:?}");
        assert!(errors[0].starts_with("in no line of the ledger"), "{errors:?}");
    }
    // A line twice: the sentence is named twice.
    let mut twice = lines.clone();
    twice.insert(1, lines[0]);
    let errors = standard::check_sentences(&sentences, &join(&twice), &tests).unwrap_err();
    assert_eq!(errors.len(), 1, "{errors:?}");
    assert!(errors[0].contains("names a sentence that a line before it names"), "{errors:?}");
    // A test that is not there.
    let wrong = LEDGER.replacen("| reason to be decided", "| test not_a_test", 1);
    let errors = standard::check_sentences(&sentences, &wrong, &tests).unwrap_err();
    assert!(errors.iter().any(|e| e.contains("does not exist")), "{errors:?}");
}

/// The path, without `.mdx`, of each page under `dir`, relative to `base`.
fn pages_under(base: &std::path::Path, dir: &std::path::Path, found: &mut Vec<String>) {
    for entry in std::fs::read_dir(dir).unwrap() {
        let path = entry.unwrap().path();
        if path.is_dir() {
            pages_under(base, &path, found);
        } else if path.extension().is_some_and(|e| e == "mdx") {
            let relative = path.strip_prefix(base).unwrap().with_extension("");
            found.push(relative.to_str().unwrap().replace('\\', "/"));
        }
    }
}

fn every_page_of_the_mcp_specification_is_taken_or_left_out_with_a_reason() {
    let base = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("..")
        .join("reference")
        .join(SPECIFICATION);
    let mut pages = Vec::new();
    pages_under(&base, &base, &mut pages);
    pages.sort();
    let mut named: Vec<String> = TAKEN
        .iter()
        .map(|p| p.to_string())
        .chain(LEFT_OUT.iter().map(|(p, _)| p.to_string()))
        .collect();
    named.sort();
    assert_eq!(pages, named, "the pages of the specification, and the pages TAKEN and LEFT_OUT name");
    assert!(
        LEFT_OUT.iter().all(|(page, why)| !why.trim().is_empty() && !TAKEN.contains(page)),
        "a page left out has a reason and is not taken"
    );
}

/// A value of JSON, as far as the examples of the schema need to be read.
#[derive(Clone, Debug, PartialEq)]
enum Json {
    Null,
    Bool(bool),
    Number(String),
    Text(String),
    Array(Vec<Json>),
    Object(Vec<(String, Json)>),
}

impl Json {
    fn get(&self, key: &str) -> Option<&Json> {
        match self {
            Json::Object(members) => members.iter().find(|(k, _)| k == key).map(|(_, v)| v),
            _ => None,
        }
    }

    fn text(&self) -> Option<&str> {
        match self {
            Json::Text(t) => Some(t),
            _ => None,
        }
    }

    fn members(&self) -> Vec<&str> {
        match self {
            Json::Object(members) => members.iter().map(|(k, _)| k.as_str()).collect(),
            _ => Vec::new(),
        }
    }
}

struct Reader<'a> {
    bytes: &'a [u8],
    at: usize,
}

fn parse(text: &str) -> Json {
    let mut reader = Reader { bytes: text.as_bytes(), at: 0 };
    let value = reader.value();
    reader.space();
    assert_eq!(reader.at, reader.bytes.len(), "text after the value");
    value
}

impl Reader<'_> {
    fn space(&mut self) {
        while self.bytes.get(self.at).is_some_and(|b| b.is_ascii_whitespace()) {
            self.at += 1;
        }
    }

    fn eat(&mut self, byte: u8) {
        self.space();
        assert_eq!(self.bytes.get(self.at), Some(&byte), "expected {} at {}", byte as char, self.at);
        self.at += 1;
    }

    fn word(&mut self, word: &str, value: Json) -> Json {
        assert!(self.bytes[self.at..].starts_with(word.as_bytes()), "expected {word} at {}", self.at);
        self.at += word.len();
        value
    }

    fn value(&mut self) -> Json {
        self.space();
        match self.bytes[self.at] {
            b'{' => {
                self.at += 1;
                let mut members = Vec::new();
                self.space();
                if self.bytes[self.at] == b'}' {
                    self.at += 1;
                    return Json::Object(members);
                }
                loop {
                    self.space();
                    let key = self.string();
                    self.eat(b':');
                    members.push((key, self.value()));
                    self.space();
                    match self.bytes[self.at] {
                        b',' => self.at += 1,
                        b'}' => {
                            self.at += 1;
                            return Json::Object(members);
                        }
                        b => panic!("expected , or }} and not {} at {}", b as char, self.at),
                    }
                }
            }
            b'[' => {
                self.at += 1;
                let mut items = Vec::new();
                self.space();
                if self.bytes[self.at] == b']' {
                    self.at += 1;
                    return Json::Array(items);
                }
                loop {
                    items.push(self.value());
                    self.space();
                    match self.bytes[self.at] {
                        b',' => self.at += 1,
                        b']' => {
                            self.at += 1;
                            return Json::Array(items);
                        }
                        b => panic!("expected , or ] and not {} at {}", b as char, self.at),
                    }
                }
            }
            b'"' => Json::Text(self.string()),
            b't' => self.word("true", Json::Bool(true)),
            b'f' => self.word("false", Json::Bool(false)),
            b'n' => self.word("null", Json::Null),
            _ => {
                let from = self.at;
                while self
                    .bytes
                    .get(self.at)
                    .is_some_and(|b| b.is_ascii_digit() || matches!(b, b'-' | b'+' | b'.' | b'e' | b'E'))
                {
                    self.at += 1;
                }
                assert!(self.at > from, "a value at {from}");
                Json::Number(String::from_utf8(self.bytes[from..self.at].to_vec()).unwrap())
            }
        }
    }

    fn string(&mut self) -> String {
        assert_eq!(self.bytes[self.at], b'"', "a string at {}", self.at);
        self.at += 1;
        let mut out: Vec<u8> = Vec::new();
        loop {
            let b = self.bytes[self.at];
            self.at += 1;
            match b {
                b'"' => return String::from_utf8(out).unwrap(),
                b'\\' => {
                    let e = self.bytes[self.at];
                    self.at += 1;
                    let c = match e {
                        b'"' => '"',
                        b'\\' => '\\',
                        b'/' => '/',
                        b'b' => '\u{8}',
                        b'f' => '\u{c}',
                        b'n' => '\n',
                        b'r' => '\r',
                        b't' => '\t',
                        b'u' => {
                            let hex = std::str::from_utf8(&self.bytes[self.at..self.at + 4]).unwrap();
                            self.at += 4;
                            let unit = u32::from_str_radix(hex, 16).unwrap();
                            char::from_u32(unit).expect("an escape that is a character, not half of a pair")
                        }
                        e => panic!("an escape {} at {}", e as char, self.at),
                    };
                    let mut buffer = [0u8; 4];
                    out.extend_from_slice(c.encode_utf8(&mut buffer).as_bytes());
                }
                b => out.push(b),
            }
        }
    }
}

/// The text of a string in a Smith program: the characters that the lexer
/// reads as more than themselves are escaped.
fn lit(s: &str) -> String {
    let mut out = String::from("\"");
    for c in s.chars() {
        match c {
            '\\' => out += "\\\\",
            '"' => out += "\\\"",
            '{' => out += "\\{",
            '}' => out += "\\}",
            '\n' => out += "\\n",
            c => out.push(c),
        }
    }
    out + "\""
}

/// The function `show`, which reads a text with `std.jsonrpc` and prints a line
/// that begins with `--` and then what it gave: the kind of message, its method
/// where it has one, its id, and the outcome of a response, one line each.
const SHOW: &str = r#"use std.console (console, print)
use std.jsonrpc

fn show(s: str) ! console, alloc {
  print("--")
  match jsonrpc.read(s) {
    .ok(.one(m)) {
      match m {
        .request(r) {
          match r.id {
            .number(n) { print("request {r.method} number {n}") }
            .text(t) { print("request {r.method} text {t}") }
            .null { print("request {r.method} null") }
          }
        }
        .notification(n) { print("notification {n.method}") }
        .response(r) {
          match r.id {
            .number(n) { print("response number {n}") }
            .text(t) { print("response text {t}") }
            .null { print("response null") }
          }
          match r.outcome {
            .ok(v) { print("result") }
            .err(f) { print("error {f.code}") }
          }
        }
      }
    }
    .ok(.many(ms)) { print("a batch of {ms.size}") }
    .err(f) { print("refused {f.code} {f.message}") }
  }
}

"#;

/// What a program that runs `show` on each text prints of each, as one line.
fn reads(texts: &[String]) -> Vec<String> {
    let calls: String = texts.iter().map(|t| format!("  show({})\n", lit(t))).collect();
    let source = format!("{SHOW}fn main() ! console, alloc {{\n{calls}}}\n");
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let outcome = scalar::run(&program);
    assert_eq!(outcome.status, 0, "for {source}");
    let mut groups: Vec<Vec<String>> = Vec::new();
    for line in String::from_utf8(outcome.stdout).unwrap().lines() {
        if line == "--" {
            groups.push(Vec::new());
        } else {
            groups.last_mut().unwrap().push(line.to_string());
        }
    }
    groups.iter().map(|g| g.join(" ")).collect()
}

/// What `show` must print of an example of the schema, as the schema says the
/// kind of message is: a request has an `id` and a `method`, a notification a
/// `method` and no `id`, a response a `result` or an `error`.
fn expected(definition: &Json, example: &Json) -> String {
    let required = match definition.get("required") {
        Some(Json::Array(names)) => names.iter().filter_map(Json::text).collect::<Vec<_>>(),
        _ => panic!("a definition of a message that says what it requires"),
    };
    for name in &required {
        assert!(example.get(name).is_some(), "the example has no {name}, which the schema requires");
    }
    let id = |example: &Json| match example.get("id") {
        Some(Json::Number(n)) => format!("number {n}"),
        Some(Json::Text(t)) => format!("text {t}"),
        other => panic!("an id that is a number or a text, not {other:?}"),
    };
    let method = |example: &Json| example.get("method").and_then(Json::text).unwrap().to_string();
    let property = |name: &str| definition.get("properties").and_then(|p| p.get(name));
    // The schema fixes the method of each kind of request and of notification.
    if let Some(constant) = property("method").and_then(|m| m.get("const")).and_then(Json::text) {
        assert_eq!(method(example), constant);
    }
    if required.contains(&"method") && required.contains(&"id") {
        format!("request {} {}", method(example), id(example))
    } else if required.contains(&"method") {
        assert!(example.get("id").is_none(), "a notification has no id");
        format!("notification {}", method(example))
    } else if required.contains(&"result") {
        format!("response {} result", id(example))
    } else if required.contains(&"error") {
        let code = match example.get("error").and_then(|e| e.get("code")) {
            Some(Json::Number(n)) => n.clone(),
            other => panic!("an error with a code, not {other:?}"),
        };
        format!("response {} error {code}", id(example))
    } else {
        panic!("a message that is none of the four: {:?}", definition.members())
    }
}

fn every_example_of_the_mcp_schema_that_is_a_message_is_read_as_the_message_it_is() {
    let schema = parse(&standard::read_under("mcp/2026-07-28/schema", "schema.json"));
    let Some(Json::Object(definitions)) = schema.get("$defs") else {
        panic!("the schema has definitions");
    };
    let dir = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("..")
        .join("reference")
        .join("mcp/2026-07-28/schema/examples");
    // A message is a definition with a member `jsonrpc`; the examples of any
    // other definition are a part of a message, or a result or an error that
    // is the member of one.
    let mut directories: Vec<_> = std::fs::read_dir(&dir).unwrap().map(|e| e.unwrap().path()).collect();
    directories.sort();
    let mut texts: Vec<String> = Vec::new();
    let mut wanted: Vec<String> = Vec::new();
    for directory in &directories {
        let name = directory.file_name().unwrap().to_str().unwrap();
        let Some((_, definition)) = definitions.iter().find(|(k, _)| k == name) else {
            panic!("an example directory with no definition in the schema: {name}");
        };
        if definition.get("properties").and_then(|p| p.get("jsonrpc")).is_none() {
            continue;
        }
        let mut files: Vec<_> = std::fs::read_dir(directory).unwrap().map(|e| e.unwrap().path()).collect();
        files.sort();
        for file in files {
            let text = std::fs::read_to_string(&file).unwrap();
            let line = expected(definition, &parse(&text));
            wanted.push(format!("{line}   <- {name}/{}", file.file_name().unwrap().to_str().unwrap()));
            texts.push(text);
        }
    }
    assert!(texts.len() >= 30, "the examples that are messages: {}", texts.len());
    let got = reads(&texts);
    assert_eq!(got.len(), texts.len(), "one line for each example: {got:?}");
    for (got, want) in got.iter().zip(&wanted) {
        let (want, example) = want.split_once("   <- ").unwrap();
        assert_eq!(got, want, "for the example {example}");
    }
}
