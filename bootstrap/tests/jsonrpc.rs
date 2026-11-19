//! `std.jsonrpc` reads and writes the messages of JSON-RPC 2.0.
//!
//! The generator builds each message as a model, writes its text itself, and
//! keeps what a reader must say of it. A right message is broken in one way
//! that the page lists, and the test says the failure `read` must give. The
//! examples of section 7 of the specification are read from its HTML.
//!
//! The programs name no type of `std.jsonrpc`: a name that a program imports
//! from it with a list leaks into the library modules it reaches, so they use
//! the module as `jsonrpc` and write what it gives in patterns.

use smith::{lower, scalar};

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "the_example_of_the_issue_is_read_and_written_as_it_says",
        the_example_of_the_issue_is_read_and_written_as_it_says,
    ),
    (
        "a_message_written_is_read_back_as_the_message_and_so_is_a_batch",
        a_message_written_is_read_back_as_the_message_and_so_is_a_batch,
    ),
    (
        "a_text_broken_in_one_way_the_page_lists_is_the_failure_the_page_names",
        a_text_broken_in_one_way_the_page_lists_is_the_failure_the_page_names,
    ),
    (
        "a_batch_with_one_member_broken_has_that_member_refused_and_the_others_read",
        a_batch_with_one_member_broken_has_that_member_refused_and_the_others_read,
    ),
    (
        "every_example_of_section_7_of_the_specification_is_read_as_it_says",
        every_example_of_section_7_of_the_specification_is_read_as_it_says,
    ),
    (
        "what_the_page_leaves_open_is_read_as_the_issue_proposes",
        what_the_page_leaves_open_is_read_as_the_issue_proposes,
    ),
    (
        "the_failures_the_standard_names_have_its_codes",
        the_failures_the_standard_names_have_its_codes,
    ),
];

#[derive(Clone, Debug)]
enum Ident {
    Number(i64),
    Text(String),
    Null,
}

/// A message as the generator means it. `params`, results, and `data` are the
/// text of a JSON value.
#[derive(Clone, Debug)]
enum Msg {
    Request { id: Ident, method: String, params: Option<String> },
    Notification { method: String, params: Option<String> },
    Success { id: Ident, result: String },
    Failed { id: Ident, code: i64, message: String, data: Option<String> },
}

/// The text of a string in JSON: with a quotation mark and a reverse solidus
/// escaped, which is all `std.json` writes.
fn json_string(s: &str) -> String {
    let mut out = String::from("\"");
    for c in s.chars() {
        match c {
            '"' => out += "\\\"",
            '\\' => out += "\\\\",
            c => out.push(c),
        }
    }
    out + "\""
}

fn ident_text(id: &Ident) -> String {
    match id {
        Ident::Number(n) => n.to_string(),
        Ident::Text(t) => json_string(t),
        Ident::Null => "null".into(),
    }
}

/// The members of a message, as `write` gives them in order, each with its
/// text.
fn members(m: &Msg) -> Vec<(&'static str, String)> {
    let mut out = vec![("jsonrpc", "\"2.0\"".to_string())];
    match m {
        Msg::Request { id, method, params } => {
            out.push(("id", ident_text(id)));
            out.push(("method", json_string(method)));
            out.extend(params.iter().map(|p| ("params", p.clone())));
        }
        Msg::Notification { method, params } => {
            out.push(("method", json_string(method)));
            out.extend(params.iter().map(|p| ("params", p.clone())));
        }
        Msg::Success { id, result } => {
            out.push(("id", ident_text(id)));
            out.push(("result", result.clone()));
        }
        Msg::Failed { id, code, message, data } => {
            out.push(("id", ident_text(id)));
            let mut error = format!("{{\"code\":{code},\"message\":{}", json_string(message));
            if let Some(d) = data {
                error += &format!(",\"data\":{d}");
            }
            out.push(("error", error + "}"));
        }
    }
    out
}

/// The text `write` must give: the members in order with no space.
fn written(m: &Msg) -> String {
    let body: Vec<String> = members(m)
        .iter()
        .map(|(name, value)| format!("\"{name}\":{value}"))
        .collect();
    format!("{{{}}}", body.join(","))
}

/// What the program `show` prints of a message read.
fn shown(m: &Msg) -> Vec<String> {
    fn id(prefix: &str, id: &Ident) -> String {
        match id {
            Ident::Number(n) => format!("{prefix} number {n}"),
            Ident::Text(t) => format!("{prefix} text {t}"),
            Ident::Null => format!("{prefix} null"),
        }
    }
    let params = |p: &Option<String>| match p {
        Some(p) => format!("params {p}"),
        None => "no params".to_string(),
    };
    match m {
        Msg::Request { id: i, method, params: p } => {
            vec![id(&format!("request {method}"), i), params(p)]
        }
        Msg::Notification { method, params: p } => {
            vec![format!("notification {method}"), params(p)]
        }
        Msg::Success { id: i, result } => vec![id("response", i), format!("result {result}")],
        Msg::Failed { id: i, code, message, data } => vec![
            id("response", i),
            format!("error {code} {message}"),
            match data {
                Some(d) => format!("data {d}"),
                None => "no data".to_string(),
            },
        ],
    }
}

/// The text of a string for a Smith program.
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

/// The `match` of a message that prints what `shown` says.
fn show_match(subject: &str, indent: &str) -> String {
    let text = format!(
        "match {subject} {{
  .request(r) {{
    match r.id {{
      .number(n) {{ print(\"request {{r.method}} number {{n}}\") }}
      .text(t) {{ print(\"request {{r.method}} text {{t}}\") }}
      .null {{ print(\"request {{r.method}} null\") }}
    }}
    match r.params {{
      .some(p) {{ print(\"params {{raw_text(p)}}\") }}
      .none {{ print(\"no params\") }}
    }}
  }}
  .notification(n) {{
    print(\"notification {{n.method}}\")
    match n.params {{
      .some(p) {{ print(\"params {{raw_text(p)}}\") }}
      .none {{ print(\"no params\") }}
    }}
  }}
  .response(r) {{
    match r.id {{
      .number(n) {{ print(\"response number {{n}}\") }}
      .text(t) {{ print(\"response text {{t}}\") }}
      .null {{ print(\"response null\") }}
    }}
    match r.outcome {{
      .ok(v) {{ print(\"result {{raw_text(v)}}\") }}
      .err(f) {{
        print(\"error {{f.code}} {{f.message}}\")
        match f.data {{
          .some(d) {{ print(\"data {{raw_text(d)}}\") }}
          .none {{ print(\"no data\") }}
        }}
      }}
    }}
  }}
}}"
    );
    text.replace('\n', &format!("\n{indent}"))
}

/// The function `show`, which reads a text and prints what it gave, and the
/// header of every program.
fn header() -> String {
    format!(
        "use std.console (console, print)\nuse std.json (raw_json, raw_text)\nuse std.jsonrpc\nuse std.text as texts\n\nfn show(s: str) ! console, alloc {{\n  match jsonrpc.read(s) {{\n    .ok(.one(m)) {{\n      {}\n    }}\n    .ok(.many(ms)) {{\n      print(\"batch {{ms.size}}\")\n      loop x in ms {{\n        match x {{\n          .ok(m) {{\n            {}\n          }}\n          .err(f) {{ print(\"member failure {{f.code}}\") }}\n        }}\n      }}\n    }}\n    .err(f) {{ print(\"failure {{f.code}} {{f.message}}\") }}\n  }}\n}}\n\n",
        show_match("m", "      "),
        show_match("m", "            ")
    )
}

fn printed(source: &str) -> Vec<String> {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let outcome = scalar::run(&program);
    assert_eq!(outcome.status, 0, "for {source}");
    String::from_utf8(outcome.stdout)
        .unwrap()
        .lines()
        .map(str::to_string)
        .collect()
}

/// A program that runs `show` on each text.
fn reads(texts: &[String]) -> Vec<String> {
    let calls: String = texts.iter().map(|t| format!("  show({})\n", lit(t))).collect();
    printed(&format!("{}fn main() ! console, alloc {{\n{calls}}}\n", header()))
}

fn failure_line(code: i64) -> String {
    let message = match code {
        -32700 => "Parse error",
        -32600 => "Invalid Request",
        -32601 => "Method not found",
        -32602 => "Invalid params",
        _ => "Internal error",
    };
    format!("failure {code} {message}")
}

fn the_example_of_the_issue_is_read_and_written_as_it_says() {
    let text = "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"ping\"}";
    assert_eq!(reads(&[text.to_string()]), ["request ping number 1", "no params"]);
    let source = format!(
        "{}fn main() ! console, alloc {{\n  t = jsonrpc.write(texts.text(), .request(jsonrpc.Request(id: .number(1), method: \"ping\", params: .none)))\n  print(texts.finish(t))\n}}\n",
        header()
    );
    assert_eq!(printed(&source), [text]);
}

fn word(g: &mut Gen) -> String {
    let letters = ['a', 'Z', '0', ' ', '.', '-', '_', '/', 'é', '語', '"', '\\'];
    (0..g.below(7)).map(|_| *g.pick(&letters)).collect()
}

fn ident(g: &mut Gen) -> Ident {
    match g.below(4) {
        0 => Ident::Number(g.below(1000) as i64 - 500),
        1 => Ident::Number(*g.pick(&[i64::MIN, i64::MAX, 0, -1])),
        2 => Ident::Text(word(g)),
        _ => Ident::Null,
    }
}

/// The text of a structured value, with the spacing the generator chose.
fn structured(g: &mut Gen) -> String {
    g.pick(&[
        "[]",
        "{}",
        "[1,2,3]",
        "[ 1 , \"a\" , null ]",
        "{\"a\":1}",
        "{ \"subtrahend\" : 23 , \"minuend\" : [42, {\"x\": true}] }",
        "[[],{},[null,false]]",
        "{\"é\":\"語\",\"s\":\"a\\\"b\"}",
    ])
    .to_string()
}

/// The text of any value.
fn value(g: &mut Gen) -> String {
    match g.below(4) {
        0 => structured(g),
        1 => g.pick(&["null", "true", "false", "0", "-12.5e3", "123456789012345678901234567890", "\"text\"", "\"\""]).to_string(),
        2 => json_string(&word(g)),
        _ => "19".to_string(),
    }
}

fn msg(g: &mut Gen) -> Msg {
    let params = |g: &mut Gen| g.one_in(2).then(|| structured(g));
    match g.below(4) {
        0 => Msg::Request { id: ident(g), method: word(g), params: params(g) },
        1 => Msg::Notification { method: word(g), params: params(g) },
        2 => Msg::Success { id: ident(g), result: value(g) },
        _ => Msg::Failed {
            id: ident(g),
            code: g.below(100000) as i64 - 50000,
            message: word(g),
            data: g.one_in(2).then(|| value(g)),
        },
    }
}

/// The Smith expression of a message, with the one value that is a `RawJson`
/// given as the variable `v`, and that value's text, when the message has one.
fn expression(m: &Msg, v: &str) -> (String, Option<String>) {
    let id = |i: &Ident| match i {
        Ident::Number(n) => format!(".number({n})"),
        Ident::Text(t) => format!(".text({})", lit(t)),
        Ident::Null => ".null".to_string(),
    };
    let params = |p: &Option<String>| if p.is_some() { format!(".some({v})") } else { ".none".to_string() };
    match m {
        Msg::Request { id: i, method, params: p } => (
            format!(".request(jsonrpc.Request(id: {}, method: {}, params: {}))", id(i), lit(method), params(p)),
            p.clone(),
        ),
        Msg::Notification { method, params: p } => (
            format!(".notification(jsonrpc.Notification(method: {}, params: {}))", lit(method), params(p)),
            p.clone(),
        ),
        Msg::Success { id: i, result } => (
            format!(".response(jsonrpc.Response(id: {}, outcome: .ok({v})))", id(i)),
            Some(result.clone()),
        ),
        Msg::Failed { id: i, code, message, data } => (
            format!(
                ".response(jsonrpc.Response(id: {}, outcome: .err(jsonrpc.Failure(code: {code}, message: {}, data: {}))))",
                id(i),
                lit(message),
                params(data)
            ),
            data.clone(),
        ),
    }
}

/// The statements that run `body` with `v` the `RawJson` of `raw`, if there is
/// one.
fn with_raw(raw: &Option<String>, v: &str, body: &str) -> String {
    match raw {
        Some(text) => format!(
            "match raw_json({}) {{\n.ok({v}) {{\n{body}}}\n.err(e) {{ print(\"not json\") }}\n}}\n",
            lit(text)
        ),
        None => body.to_string(),
    }
}

fn a_message_written_is_read_back_as_the_message_and_so_is_a_batch() {
    generate::check(
        6701,
        3,
        |g| {
            let singles: Vec<Msg> = (0..10).map(|_| msg(g)).collect();
            let batches: Vec<Vec<Msg>> = (0..3).map(|_| (0..1 + g.below(3)).map(|_| msg(g)).collect()).collect();
            (singles, batches)
        },
        |(singles, batches)| {
            let mut functions = String::new();
            let mut calls = String::new();
            let mut lines = Vec::new();
            for (i, m) in singles.iter().enumerate() {
                let (e, raw) = expression(m, "v");
                let body = format!(
                    "t = jsonrpc.write(texts.text(), {e})\ns = texts.finish(t)\nprint(s)\nshow(s)\n"
                );
                functions += &format!("fn single{i}() ! console, alloc {{\n{}}}\n\n", with_raw(&raw, "v", &body));
                calls += &format!("  single{i}()\n");
                lines.push(written(m));
                lines.extend(shown(m));
            }
            for (i, ms) in batches.iter().enumerate() {
                // Each message that has a value takes a `RawJson` of its own.
                let made: Vec<(String, Option<String>)> =
                    ms.iter().enumerate().map(|(k, m)| expression(m, &format!("v{k}"))).collect();
                let items: Vec<&str> = made.iter().map(|(e, _)| e.as_str()).collect();
                let mut body = format!(
                    "t = jsonrpc.write_batch(texts.text(), [{}])\ns = texts.finish(t)\nprint(s)\nshow(s)\n",
                    items.join(", ")
                );
                for (k, (_, raw)) in made.iter().enumerate().rev() {
                    body = with_raw(raw, &format!("v{k}"), &body);
                }
                functions += &format!("fn batch{i}() ! console, alloc {{\n{body}}}\n\n");
                calls += &format!("  batch{i}()\n");
                let text: Vec<String> = ms.iter().map(written).collect();
                lines.push(format!("[{}]", text.join(",")));
                lines.push(format!("batch {}", ms.len()));
                for m in ms {
                    lines.extend(shown(m));
                }
            }
            let source = format!("{}{functions}fn main() ! console, alloc {{\n{calls}}}\n", header());
            assert_eq!(printed(&source), lines);
        },
    );
}

/// The text of an object of these members, with the spacing the generator
/// chose and the members turned round by `turn`.
fn render(g: &mut Gen, members: &[(String, String)], turn: usize) -> String {
    let mut ms = members.to_vec();
    if !ms.is_empty() {
        let k = turn % ms.len();
        ms.rotate_left(k);
    }
    let comma = *g.pick(&[",", ", ", " , "]);
    let colon = *g.pick(&[":", ": ", " : "]);
    let around = *g.pick(&["", " ", "\n"]);
    let body: Vec<String> = ms.iter().map(|(n, v)| format!("\"{n}\"{colon}{v}")).collect();
    format!("{around}{{{}}}{around}", body.join(comma))
}

fn owned(m: &Msg) -> Vec<(String, String)> {
    members(m).into_iter().map(|(n, v)| (n.to_string(), v)).collect()
}

fn set(ms: &mut Vec<(String, String)>, name: &str, value: &str) {
    match ms.iter_mut().find(|(n, _)| n == name) {
        Some(m) => m.1 = value.to_string(),
        None => ms.push((name.to_string(), value.to_string())),
    }
}

fn is_response(m: &Msg) -> bool {
    matches!(m, Msg::Success { .. } | Msg::Failed { .. })
}

/// A right message with one member broken in a way the page lists for a value
/// that is no message, as members, or `None` for a way that is not a matter of
/// members.
fn break_members(g: &mut Gen, m: &Msg) -> Vec<(String, String)> {
    let mut ms = owned(m);
    let response = is_response(m);
    let request = matches!(m, Msg::Request { .. });
    let mut kinds: Vec<usize> = vec![0, 1, 7];
    if !response {
        kinds.extend([2, 4, 8]);
    }
    if request {
        kinds.push(3);
    }
    if response {
        kinds.extend([5, 6, 9]);
    }
    if matches!(m, Msg::Failed { .. }) {
        kinds.push(10);
    }
    match *g.pick(&kinds) {
        0 => ms.retain(|(n, _)| n != "jsonrpc"),
        1 => {
            let v = *g.pick(&["\"1.0\"", "2", "\"2\"", "null", "2.0", "[\"2.0\"]", "\"2.0 \""]);
            set(&mut ms, "jsonrpc", v)
        }
        2 => {
            let v = *g.pick(&["1", "null", "true", "[]", "{}"]);
            set(&mut ms, "method", v)
        }
        3 => {
            let v = *g.pick(&["true", "[]", "{}", "1.5", "1e2", "-0.5", "1.0", "2E1", "10e-1", "0.0", "99999999999999999999", "-9223372036854775809", "9223372036854775808"]);
            set(&mut ms, "id", v)
        }
        4 => {
            let v = *g.pick(&["null", "1", "\"x\"", "true", "false"]);
            set(&mut ms, "params", v)
        }
        5 => {
            if ms.iter().any(|(n, _)| n == "result") {
                set(&mut ms, "error", "{\"code\":1,\"message\":\"m\"}")
            } else {
                set(&mut ms, "result", "1")
            }
        }
        6 => ms.retain(|(n, _)| n != "result" && n != "error"),
        7 => {
            let at = g.below(ms.len());
            let again = if g.one_in(2) { ms[at].1.clone() } else { "0".to_string() };
            ms.push((ms[at].0.clone(), again));
        }
        8 => {
            let name = *g.pick(&["result", "error"]);
            let v = if name == "result" { "1" } else { "{\"code\":1,\"message\":\"m\"}" };
            set(&mut ms, name, v)
        }
        9 => ms.retain(|(n, _)| n != "id"),
        _ => {
            let v = *g.pick(&[
                "1",
                "\"x\"",
                "null",
                "[]",
                "{\"code\":1.5,\"message\":\"m\"}",
                "{\"code\":\"1\",\"message\":\"m\"}",
                "{\"message\":\"m\"}",
                "{\"code\":1}",
                "{\"code\":1,\"message\":2}",
                "{\"code\":99999999999999999999,\"message\":\"m\"}",
            ]);
            set(&mut ms, "error", v)
        }
    }
    ms
}

/// A text with one fault of the page's list put in, and the code of the failure
/// `read` must give.
fn broken(g: &mut Gen) -> (String, i64) {
    let m = msg(g);
    let turn = g.below(4);
    match g.below(8) {
        // A text that is not JSON.
        0 => {
            let mut t = written(&m);
            t.pop();
            (t, -32700)
        }
        1 => (format!("{} x", written(&m)), -32700),
        2 => {
            let t = written(&m);
            (format!("{},}}", &t[..t.len() - 1]), -32700)
        }
        3 => ((*g.pick(&["", " ", "{", "[", "}", "\"a", "nul", "{\"a\":}", "[1,]"])).to_string(), -32700),
        // A value that is no message.
        4 => ((*g.pick(&["3", "\"x\"", "null", "true", "[]", "  []  ", "{}", "-1.5"])).to_string(), -32600),
        _ => {
            let ms = break_members(g, &m);
            (render(g, &ms, turn), -32600)
        }
    }
}

fn a_text_broken_in_one_way_the_page_lists_is_the_failure_the_page_names() {
    generate::check(
        6702,
        3,
        |g| (0..40).map(|_| broken(g)).collect::<Vec<_>>(),
        |batch| {
            let texts: Vec<String> = batch.iter().map(|(t, _)| t.clone()).collect();
            let expected: Vec<String> = batch.iter().map(|(_, c)| failure_line(*c)).collect();
            assert_eq!(reads(&texts), expected, "for {batch:?}");
        },
    );
}

fn a_batch_with_one_member_broken_has_that_member_refused_and_the_others_read() {
    generate::check(
        6703,
        3,
        |g| {
            (0..8)
                .map(|_| {
                    let ms: Vec<Msg> = (0..2 + g.below(3)).map(|_| msg(g)).collect();
                    let at = g.below(ms.len());
                    let turn = g.below(3);
                    let how = g.below(3);
                    let member = match how {
                        0 => (*g.pick(&["1", "\"x\"", "null", "[]", "[{}]", "true"])).to_string(),
                        _ => {
                            let broken = break_members(g, &ms[at]);
                            render(g, &broken, turn)
                        }
                    };
                    (ms, at, member)
                })
                .collect::<Vec<_>>()
        },
        |cases| {
            let mut texts = Vec::new();
            let mut lines = Vec::new();
            for (ms, at, member) in cases {
                let parts: Vec<String> = ms
                    .iter()
                    .enumerate()
                    .map(|(i, m)| if i == *at { member.clone() } else { written(m) })
                    .collect();
                texts.push(format!("[{}]", parts.join(", ")));
                lines.push(format!("batch {}", ms.len()));
                for (i, m) in ms.iter().enumerate() {
                    if i == *at {
                        lines.push("member failure -32600".to_string());
                    } else {
                        lines.extend(shown(m));
                    }
                }
            }
            assert_eq!(reads(&texts), lines);
        },
    );
}

/// What a program that prints one line for each member of what it reads says.
fn brief_header() -> String {
    let member = |subject: &str| {
        format!(
            "match {subject} {{\n.request(r) {{\nmatch r.id {{\n.number(n) {{ print(\"request number {{n}}\") }}\n.text(t) {{ print(\"request text {{t}}\") }}\n.null {{ print(\"request null\") }}\n}}\n}}\n.notification(n) {{ print(\"notification\") }}\n.response(r) {{ print(\"response\") }}\n}}\n"
        )
    };
    format!(
        "use std.console (console, print)\nuse std.jsonrpc\n\nfn brief(s: str) ! console, alloc {{\n  match jsonrpc.read(s) {{\n    .ok(.one(m)) {{\n{}}}\n    .ok(.many(ms)) {{\n      print(\"batch {{ms.size}}\")\n      loop x in ms {{\n        match x {{\n          .ok(m) {{\n{}}}\n          .err(f) {{ print(\"member failure {{f.code}}\") }}\n        }}\n      }}\n    }}\n    .err(f) {{ print(\"failure {{f.code}}\") }}\n  }}\n}}\n\n",
        member("m"),
        member("m")
    )
}

/// An example of section 7: what a client sent, each with the response shown
/// for it, if any.
struct Example {
    sent: String,
    got: Option<String>,
}

fn text_of_pre(pre: &str) -> String {
    let mut out = String::new();
    let mut rest = pre;
    while let Some(open) = rest.find('<') {
        out += &rest[..open];
        let close = rest[open..].find('>').unwrap();
        if rest[open + 1..open + close].starts_with("br") {
            out.push('\n');
        }
        rest = &rest[open + close + 1..];
    }
    out += rest;
    out.replace("&gt;", ">").replace("&lt;", "<").replace("&nbsp;", " ").replace("&amp;", "&")
}

/// Every pair of a text sent and a text given back in the examples of section
/// 7 of the specification, as the page writes them after `-->` and `<--`.
fn examples(html: &str) -> Vec<Example> {
    let from = html.find("id=\"examples\"").unwrap();
    let to = html.find("id=\"extensions\"").unwrap();
    let mut rest = &html[from..to];
    let mut pairs: Vec<Example> = Vec::new();
    let mut first = true;
    while let Some(open) = rest.find("<pre>") {
        let close = rest.find("</pre>").unwrap();
        let text = text_of_pre(&rest[open + 5..close]);
        rest = &rest[close + 6..];
        if std::mem::take(&mut first) {
            // The syntax of the examples, which is no example.
            continue;
        }
        let mut now: Option<bool> = None;
        for line in text.lines() {
            if let Some(sent) = line.strip_prefix("--> ") {
                pairs.push(Example { sent: sent.to_string(), got: None });
                now = Some(true);
            } else if let Some(got) = line.strip_prefix("<-- ") {
                pairs.last_mut().unwrap().got = Some(got.to_string());
                now = Some(false);
            } else if !line.trim().is_empty() {
                let last = pairs.last_mut().unwrap();
                let into = if now == Some(true) { &mut last.sent } else { last.got.as_mut().unwrap() };
                into.push('\n');
                into.push_str(line);
            }
        }
    }
    pairs
}

/// The members of the text of an array, each as written, cut at the commas
/// that are not inside a value.
fn array_members(text: &str) -> Vec<String> {
    let inner = text.trim().strip_prefix('[').and_then(|t| t.strip_suffix(']')).unwrap_or("");
    let (mut depth, mut quoted, mut escaped) = (0, false, false);
    let mut members = vec![String::new()];
    for c in inner.chars() {
        if quoted {
            if escaped {
                escaped = false;
            } else if c == '\\' {
                escaped = true;
            } else if c == '"' {
                quoted = false;
            }
        } else if c == '"' {
            quoted = true;
        } else if c == '{' || c == '[' {
            depth += 1;
        } else if c == '}' || c == ']' {
            depth -= 1;
        } else if c == ',' && depth == 0 {
            members.push(String::new());
            continue;
        }
        members.last_mut().unwrap().push(c);
    }
    members.retain(|m| !m.trim().is_empty());
    members
}

/// The value written after `"name":` in the text of a response, which the
/// examples write last or, for the code, before a comma.
fn after(text: &str, name: &str) -> String {
    let at = text.rfind(&format!("\"{name}\":")).unwrap() + name.len() + 3;
    let value = text[at..].trim_start();
    let end = value.find([',', '}']).unwrap_or(value.len());
    value[..end].trim().to_string()
}

/// What `brief` prints of a request whose response is this one.
fn answered_request(response: &str) -> String {
    let id = after(response, "id");
    match id.strip_prefix('"') {
        Some(text) => format!("request text {}", text.trim_end_matches('"')),
        None => format!("request number {id}"),
    }
}

fn every_example_of_section_7_of_the_specification_is_read_as_it_says() {
    let html = std::fs::read_to_string(concat!(env!("CARGO_MANIFEST_DIR"), "/../reference/jsonrpc/specification.html")).unwrap();
    let found = examples(&html);
    assert!(found.len() >= 15, "the examples of section 7: {}", found.len());
    let texts: Vec<String> = found.iter().map(|e| e.sent.clone()).collect();
    let calls: String = texts.iter().map(|t| format!("  print(\"--\")\n  brief({})\n", lit(t))).collect();
    let source = format!("{}fn main() ! console, alloc {{\n{calls}}}\n", brief_header());
    let output = printed(&source);
    let mut got: Vec<Vec<String>> = Vec::new();
    for line in output {
        if line == "--" {
            got.push(Vec::new());
        } else {
            got.last_mut().unwrap().push(line);
        }
    }
    assert_eq!(got.len(), found.len());
    for (example, lines) in found.iter().zip(&got) {
        let response = example.got.as_deref().filter(|g| !g.starts_with("//"));
        let mut want: Vec<String> = Vec::new();
        let sent = example.sent.trim_start();
        match response {
            // No response: a notification, or a batch of them.
            None if sent.starts_with('[') => {
                let n = array_members(sent).len();
                want.push(format!("batch {n}"));
                want.extend(vec!["notification".to_string(); n]);
            }
            None => want.push("notification".to_string()),
            Some(r) if r.trim_start().starts_with('[') => {
                // A batch: each response is a member refused when its id is null,
                // a request when it is not, and the others are notifications.
                let responses = array_members(r);
                let members = array_members(sent).len();
                want.push(format!("batch {members}"));
                for each in &responses {
                    if after(each, "id") == "null" {
                        want.push(format!("member failure {}", after(each, "code")));
                    } else {
                        want.push(answered_request(each));
                    }
                }
                want.extend(vec!["notification".to_string(); members - responses.len()]);
            }
            Some(r) if r.contains("\"error\"") && matches!(after(r, "code").as_str(), "-32700" | "-32600") => {
                want.push(format!("failure {}", after(r, "code")));
            }
            Some(r) => want.push(answered_request(r)),
        }
        let mut sorted = lines.clone();
        // The members of a batch come in the order they were sent, which the
        // responses do not.
        let (mut a, mut b) = (want.clone(), sorted.split_off(0));
        if want.first().is_some_and(|l| l.starts_with("batch")) {
            let (head_a, head_b) = (a.remove(0), b.remove(0));
            assert_eq!(head_a, head_b, "for {}", example.sent);
            a.sort();
            b.sort();
        }
        assert_eq!(a, b, "for {}", example.sent);
    }
}

fn the_failures_the_standard_names_have_its_codes() {
    let source = format!(
        "{}fn line(f: jsonrpc.Failure) ! console, alloc {{\n}}\n",
        "use std.console (console, print)\nuse std.jsonrpc\n\n"
    );
    let _ = source;
    let source = "use std.console (console, print)\nuse std.jsonrpc\n\nfn main() ! console, alloc {\n  a = jsonrpc.parse_error()\n  print(\"{a.code} {a.message}\")\n  b = jsonrpc.invalid_request()\n  print(\"{b.code} {b.message}\")\n  c = jsonrpc.method_not_found(\"nothing\")\n  print(\"{c.code} {c.message}\")\n  d = jsonrpc.invalid_params(\"x must be a number\")\n  print(\"{d.code} {d.message}\")\n  e = jsonrpc.internal_error()\n  print(\"{e.code} {e.message}\")\n  match e.data {\n    .some(v) { print(\"data\") }\n    .none { print(\"no data\") }\n  }\n}\n";
    assert_eq!(
        printed(source),
        [
            "-32700 Parse error",
            "-32600 Invalid Request",
            "-32601 Method not found",
            "-32602 x must be a number",
            "-32603 Internal error",
            "no data"
        ]
    );
}

fn what_the_page_leaves_open_is_read_as_the_issue_proposes() {
    let cases: Vec<(&str, Vec<&str>)> = vec![
        // A request whose id is null is a request, and a missing id is a notification.
        ("{\"jsonrpc\":\"2.0\",\"id\":null,\"method\":\"a\"}", vec!["request a null", "no params"]),
        ("{\"jsonrpc\":\"2.0\",\"method\":\"a\"}", vec!["notification a", "no params"]),
        // Members the standard does not name are left out.
        ("{\"jsonrpc\":\"2.0\",\"method\":\"a\",\"foo\":[1,{\"x\":null}],\"id\":3}", vec!["request a number 3", "no params"]),
        // A method that begins with `rpc.` is read as any method.
        ("{\"jsonrpc\":\"2.0\",\"method\":\"rpc.x\",\"id\":3}", vec!["request rpc.x number 3", "no params"]),
        // The version is a text that is 2.0, written any way.
        ("{\"jsonrpc\":\"2\\u002e0\",\"method\":\"a\"}", vec!["notification a", "no params"]),
        // Numbers: no fraction, no exponent, nothing beyond an `Int`; minus zero is zero.
        ("{\"jsonrpc\":\"2.0\",\"id\":-0,\"method\":\"a\"}", vec!["request a number 0", "no params"]),
        ("{\"jsonrpc\":\"2.0\",\"id\":-9223372036854775808,\"method\":\"a\"}", vec!["request a number -9223372036854775808", "no params"]),
        ("{\"jsonrpc\":\"2.0\",\"id\":9223372036854775807,\"method\":\"a\"}", vec!["request a number 9223372036854775807", "no params"]),
        // Parameters are text as they came, with their spaces.
        ("{\"jsonrpc\":\"2.0\",\"method\":\"a\",\"params\":[ 1 ,2 ]}", vec!["notification a", "params [ 1 ,2 ]"]),
        // A result of null is a result, and data of null is data.
        ("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":null}", vec!["response number 1", "result null"]),
        ("{\"jsonrpc\":\"2.0\",\"id\":null,\"error\":{\"code\":-1,\"message\":\"m\",\"data\":null,\"more\":1}}", vec!["response null", "error -1 m", "data null"]),
        ("{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":{\"code\":-1,\"message\":\"m\"}}", vec!["response number 1", "error -1 m", "no data"]),
        // Spaces round the value are no part of it.
        ("  {\"jsonrpc\":\"2.0\",\"method\":\"a\"}\n", vec!["notification a", "no params"]),
    ];
    let texts: Vec<String> = cases.iter().map(|(t, _)| t.to_string()).collect();
    let want: Vec<String> = cases.iter().flat_map(|(_, l)| l.iter().map(|x| x.to_string())).collect();
    assert_eq!(reads(&texts), want);
}
