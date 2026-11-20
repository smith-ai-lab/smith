//! `std.mcp` answers one message of a client with one reply.
//!
//! The generator builds each message and keeps what it has put in it: a
//! request that is right, or one that is broken in one way that the text of the
//! protocol lists. The test says the status, the failure, and the id the reply
//! must have, and reads the reply as JSON. What `server/discover` gives is held
//! to the members that the schema of the protocol names, and to the ones it
//! requires.
//!
//! The programs name no type of `std.mcp`: a name that a program imports from a
//! module of the library with a list leaks into the modules it reaches.

use std::sync::OnceLock;

use smith::{lower, scalar};

use crate::Test;
use crate::generate::{self, Gen};
use crate::standard::{self, Json, lit, parse};

pub const ONCE: &[Test] = &[
    (
        "a_discover_request_is_answered_with_the_result_the_schema_names",
        a_discover_request_is_answered_with_the_result_the_schema_names,
    ),
    (
        "a_request_with_a_version_or_a_meta_that_is_wrong_is_refused_with_the_failure_the_text_names",
        a_request_with_a_version_or_a_meta_that_is_wrong_is_refused_with_the_failure_the_text_names,
    ),
    (
        "a_method_the_server_has_not_is_answered_not_found_and_a_notification_by_none",
        a_method_the_server_has_not_is_answered_not_found_and_a_notification_by_none,
    ),
    (
        "a_header_that_does_not_hold_to_the_message_is_refused_and_one_that_does_is_not",
        a_header_that_does_not_hold_to_the_message_is_refused_and_one_that_does_is_not,
    ),
    (
        "a_name_in_a_header_is_held_to_the_name_in_the_message_as_written_or_in_base64",
        a_name_in_a_header_is_held_to_the_name_in_the_message_as_written_or_in_base64,
    ),
    (
        "a_text_that_is_no_request_is_refused_and_the_reply_has_no_id_where_none_was_read",
        a_text_that_is_no_request_is_refused_and_the_reply_has_no_id_where_none_was_read,
    ),
    (
        "a_message_of_a_page_that_the_server_does_not_list_is_refused_before_it_is_read",
        a_message_of_a_page_that_the_server_does_not_list_is_refused_before_it_is_read,
    ),
];

// The numbers and the version that the text of the protocol gives.
const VERSION: &str = "2026-07-28";
const PARSE_ERROR: i64 = -32700;
const INVALID_REQUEST: i64 = -32600;
const METHOD_NOT_FOUND: i64 = -32601;
const INVALID_PARAMS: i64 = -32602;
const HEADER_MISMATCH: i64 = -32020;
const UNSUPPORTED_PROTOCOL_VERSION: i64 = -32022;

const PROTOCOL_VERSION_KEY: &str = "io.modelcontextprotocol/protocolVersion";
const CAPABILITIES_KEY: &str = "io.modelcontextprotocol/clientCapabilities";
const SERVER_INFO_KEY: &str = "io.modelcontextprotocol/serverInfo";

/// What a server says of itself and holds a message to.
#[derive(Clone, Debug)]
struct Spec {
    name: String,
    version: String,
    instructions: Option<String>,
    ttl: String,
    origins: Vec<String>,
}

/// What the reply to a message must be.
#[derive(Clone, Debug)]
enum Want {
    /// The result of `server/discover`, with the id of the request.
    Discovered { id: String },
    /// A failure with this status and code, the id written as JSON where one
    /// was read, and the text of `data` where the failure has it.
    Failed { status: u16, code: i64, id: Option<String>, data: Option<String> },
    /// No text, and this status.
    Silent { status: u16 },
}

#[derive(Clone, Debug)]
struct Case {
    server: Spec,
    headers: Vec<(String, String)>,
    text: String,
    want: Want,
}

/// What the program printed for a message.
#[derive(Debug)]
struct Got {
    status: u16,
    text: Option<String>,
}

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

fn word(g: &mut Gen) -> String {
    let letters = ['a', 'Z', '0', ' ', '.', '-', '_', '/', 'é', '語', '"', '\\'];
    (0..1 + g.below(6)).map(|_| *g.pick(&letters)).collect()
}

/// The text of an id: a whole number, or a text.
fn id_text(g: &mut Gen) -> String {
    match g.below(3) {
        0 => (g.below(1000) as i64 - 500).to_string(),
        1 => g.pick(&["0", "-1", "9223372036854775807", "-9223372036854775808"]).to_string(),
        _ => json_string(&word(g)),
    }
}

fn spec(g: &mut Gen) -> Spec {
    let origins = ["https://notes.example", "http://localhost:3000", "https://other.example"];
    Spec {
        name: word(g),
        version: word(g),
        instructions: g.one_in(2).then(|| word(g)),
        ttl: g.pick(&["0", "1", "300000", "18446744073709551615"]).to_string(),
        origins: origins.iter().filter(|_| g.one_in(2)).map(|s| s.to_string()).collect(),
    }
}

/// The text of an object of these members, as the generator spaces them.
fn object(g: &mut Gen, members: &[(String, String)]) -> String {
    let mut ms = members.to_vec();
    if ms.len() > 1 && g.one_in(2) {
        let by = 1 + g.below(ms.len() - 1);
        ms.rotate_left(by);
    }
    let (open, between) = if g.one_in(2) { ("{ ", " , ") } else { ("{", ",") };
    let body: Vec<String> = ms.iter().map(|(k, v)| format!("{}:{v}", json_string(k))).collect();
    format!("{open}{}{}", body.join(between), if open == "{ " { " }" } else { "}" })
}

fn capabilities(g: &mut Gen) -> String {
    g.pick(&["{}", "{\"roots\":{}}", "{\"sampling\":{},\"elicitation\":{\"form\":{}}}"]).to_string()
}

/// The members of `_meta` that a request has, with the version it says.
fn meta_members(g: &mut Gen, version: &str) -> Vec<(String, String)> {
    let mut ms = vec![
        (PROTOCOL_VERSION_KEY.to_string(), json_string(version)),
        (CAPABILITIES_KEY.to_string(), capabilities(g)),
    ];
    if g.one_in(2) {
        ms.push(("io.modelcontextprotocol/clientInfo".into(), "{\"name\":\"ExampleClient\",\"version\":\"1.0.0\"}".into()));
    }
    if g.one_in(3) {
        ms.push(("progressToken".into(), "\"abc123\"".into()));
    }
    if g.one_in(3) {
        ms.push(("traceparent".into(), "\"00-0af7651916cd43dd8448eb211c80319c-00f067aa0ba902b7-01\"".into()));
    }
    ms
}

/// The text of the `_meta` of a request that says this version.
fn meta_text(g: &mut Gen, version: &str) -> String {
    let members = meta_members(g, version);
    object(g, &members)
}

/// The text of a request with a `params` that holds `_meta` and these members.
fn request(g: &mut Gen, id: &str, method: &str, meta: &str, extra: &[(String, String)]) -> String {
    let mut params = vec![("_meta".to_string(), meta.to_string())];
    params.extend(extra.iter().cloned());
    let members = vec![
        ("jsonrpc".to_string(), "\"2.0\"".to_string()),
        ("id".to_string(), id.to_string()),
        ("method".to_string(), json_string(method)),
        ("params".to_string(), object(g, &params)),
    ];
    object(g, &members)
}

/// The headers of a request that came over HTTP, with the ones the protocol
/// asks for and some it does not.
fn http_headers(g: &mut Gen, version: &str, method: &str) -> Vec<(String, String)> {
    let mut hs = vec![
        ("host".to_string(), "notes.example".to_string()),
        ("content-type".to_string(), "application/json".to_string()),
        ("accept".to_string(), "application/json, text/event-stream".to_string()),
        ("mcp-protocol-version".to_string(), version.to_string()),
        ("mcp-method".to_string(), method.to_string()),
    ];
    if g.one_in(2) {
        hs.rotate_left(1 + g.below(4));
    }
    hs
}

fn right_request(g: &mut Gen, id: &str, method: &str) -> (String, Vec<(String, String)>) {
    let meta = meta_text(g, VERSION);
    let extra = if g.one_in(3) { vec![("extra".to_string(), "{\"a\":[1,2]}".to_string())] } else { Vec::new() };
    let text = request(g, id, method, &meta, &extra);
    let headers = if g.one_in(2) { http_headers(g, VERSION, method) } else { Vec::new() };
    (text, headers)
}

fn failed(status: u16, code: i64, id: Option<&str>) -> Want {
    Want::Failed { status, code, id: id.map(str::to_string), data: None }
}

fn discover_case(g: &mut Gen) -> Case {
    let id = id_text(g);
    let (text, headers) = right_request(g, &id, "server/discover");
    Case { server: spec(g), headers, text, want: Want::Discovered { id } }
}

/// A request that is right, whose method the server has not.
fn unknown_method_case(g: &mut Gen) -> Case {
    let id = id_text(g);
    let method = g.pick(&["tools/list", "resources/list", "prompts/list", "subscriptions/listen", "ping", "x/y", ""]).to_string();
    let (text, headers) = right_request(g, &id, &method);
    Case { server: spec(g), headers, text, want: failed(404, METHOD_NOT_FOUND, Some(&id)) }
}

/// A request that says another version of the protocol than the server speaks.
fn other_version_case(g: &mut Gen) -> Case {
    let id = id_text(g);
    let version = g.pick(&["2025-11-25", "1900-01-01", "2026-07-29", "", "2026-07-28 "]).to_string();
    let meta = meta_text(g, &version);
    let text = request(g, &id, "server/discover", &meta, &[]);
    let headers = if g.one_in(2) { http_headers(g, &version, "server/discover") } else { Vec::new() };
    let data = format!("{{\"supported\":[{}],\"requested\":{}}}", json_string(VERSION), json_string(&version));
    Case {
        server: spec(g),
        headers,
        text,
        want: Want::Failed { status: 400, code: UNSUPPORTED_PROTOCOL_VERSION, id: Some(id), data: Some(data) },
    }
}

/// A client of an earlier version of the protocol, which opens with `initialize`.
fn initialize_case(g: &mut Gen) -> Case {
    let id = id_text(g);
    let version = g.pick(&["2025-11-25", "2025-03-26", "2024-11-05"]).to_string();
    let params = if g.one_in(4) {
        None
    } else {
        Some(format!(
            "{{\"protocolVersion\":{},\"capabilities\":{{}},\"clientInfo\":{{\"name\":\"c\",\"version\":\"1\"}}}}",
            json_string(&version)
        ))
    };
    let mut members = vec![
        ("jsonrpc".to_string(), "\"2.0\"".to_string()),
        ("id".to_string(), id.clone()),
        ("method".to_string(), "\"initialize\"".to_string()),
    ];
    let requested = match &params {
        Some(p) => {
            members.push(("params".to_string(), p.clone()));
            version
        }
        None => String::new(),
    };
    let data = format!("{{\"supported\":[{}],\"requested\":{}}}", json_string(VERSION), json_string(&requested));
    Case {
        server: spec(g),
        headers: Vec::new(),
        text: object(g, &members),
        want: Want::Failed { status: 400, code: UNSUPPORTED_PROTOCOL_VERSION, id: Some(id), data: Some(data) },
    }
}

/// A request whose `params` lacks one of what the protocol asks every request to say.
fn bad_meta_case(g: &mut Gen) -> Case {
    let id = id_text(g);
    let version = json_string(VERSION);
    let params: Option<String> = match g.below(7) {
        0 => None,
        1 => Some("{}".to_string()),
        2 => Some("{\"_meta\":3}".to_string()),
        3 => Some(format!("{{\"_meta\":{{\"{CAPABILITIES_KEY}\":{{}}}}}}")),
        4 => Some(format!("{{\"_meta\":{{\"{PROTOCOL_VERSION_KEY}\":7,\"{CAPABILITIES_KEY}\":{{}}}}}}")),
        5 => Some(format!("{{\"_meta\":{{\"{PROTOCOL_VERSION_KEY}\":{version}}}}}")),
        _ => Some(format!("{{\"_meta\":{{\"{PROTOCOL_VERSION_KEY}\":{version},\"{CAPABILITIES_KEY}\":[]}}}}")),
    };
    let mut members = vec![
        ("jsonrpc".to_string(), "\"2.0\"".to_string()),
        ("id".to_string(), id.clone()),
        ("method".to_string(), "\"server/discover\"".to_string()),
    ];
    if let Some(p) = params {
        members.push(("params".to_string(), p));
    }
    let headers = if g.one_in(2) { http_headers(g, VERSION, "server/discover") } else { Vec::new() };
    Case { server: spec(g), headers, text: object(g, &members), want: failed(400, INVALID_PARAMS, Some(&id)) }
}

/// A notification, whatever it says and however it came: no text, and 202.
fn notification_case(g: &mut Gen) -> Case {
    let method = g.pick(&["notifications/cancelled", "notifications/initialized", "x/y", "server/discover"]).to_string();
    let params = g.pick(&["{\"requestId\":1}", "{}", "[1]", "{\"_meta\":3}"]).to_string();
    let mut members = vec![
        ("jsonrpc".to_string(), "\"2.0\"".to_string()),
        ("method".to_string(), json_string(&method)),
    ];
    if g.one_in(2) {
        members.push(("params".to_string(), params));
    }
    let headers = if g.one_in(2) { http_headers(g, VERSION, &method) } else { Vec::new() };
    Case { server: spec(g), headers, text: object(g, &members), want: Want::Silent { status: 202 } }
}

/// A text that is no request: the failure is the one for a text that is no
/// JSON or no message, and it has no id, as the protocol writes it.
fn no_request_case(g: &mut Gen) -> Case {
    let (text, code): (&str, i64) = *g.pick(&[
        ("not json", PARSE_ERROR),
        ("", PARSE_ERROR),
        ("{", PARSE_ERROR),
        ("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":", PARSE_ERROR),
        ("3", INVALID_REQUEST),
        ("\"x\"", INVALID_REQUEST),
        ("null", INVALID_REQUEST),
        ("[]", INVALID_REQUEST),
        ("{}", INVALID_REQUEST),
        // A response, which a client does not send.
        ("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{}}", INVALID_REQUEST),
        // A batch, of which a post holds none.
        ("[{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"server/discover\"}]", INVALID_REQUEST),
        // A request with an id that is `null`, which the protocol does not allow.
        ("{\"jsonrpc\":\"2.0\",\"id\":null,\"method\":\"server/discover\",\"params\":{}}", INVALID_REQUEST),
    ]);
    let headers = if g.one_in(2) { http_headers(g, VERSION, "server/discover") } else { Vec::new() };
    Case { server: spec(g), headers, text: text.to_string(), want: failed(200, code, None) }
}

/// A request whose `params` is an array, where the protocol has an object: the
/// id was read, so the failure has it.
fn array_params_case(g: &mut Gen) -> Case {
    let id = id_text(g);
    let members = vec![
        ("jsonrpc".to_string(), "\"2.0\"".to_string()),
        ("id".to_string(), id.clone()),
        ("method".to_string(), "\"server/discover\"".to_string()),
        ("params".to_string(), g.pick(&["[]", "[1,2]", "[{\"_meta\":{}}]"]).to_string()),
    ];
    Case { server: spec(g), headers: Vec::new(), text: object(g, &members), want: failed(200, INVALID_REQUEST, Some(&id)) }
}

/// Runs the program that gives each case to `reply_to` and prints what it gave.
fn replies(cases: &[Case]) -> Vec<Got> {
    let mut source = String::from(
        "use std.console (console, print)\nuse std.mcp\n\nfn run(name: str, version: str, instructions: Option[str], ttl: Nat, origins: take List[str], text: str, headers: take List[(str, str)]) ! console, alloc {\n  r = mcp.reply_to(mcp.Server(name: name, version: version, instructions: instructions, ttl: ttl, origins: origins), mcp.Incoming(text: text, headers: headers))\n  print(\"--\")\n  print(\"{r.status}\")\n  match r.text {\n    .some(t) { print(t) }\n    .none { print(\"none\") }\n  }\n}\n\nfn main() ! console, alloc {\n",
    );
    for c in cases {
        let instructions = match &c.server.instructions {
            Some(i) => format!(".some({})", lit(i)),
            None => ".none".to_string(),
        };
        let origins: Vec<String> = c.server.origins.iter().map(|o| lit(o)).collect();
        let headers: Vec<String> = c.headers.iter().map(|(n, v)| format!("({}, {})", lit(n), lit(v))).collect();
        source += &format!(
            "  run({}, {}, {instructions}, {}, [{}], {}, [{}])\n",
            lit(&c.server.name),
            lit(&c.server.version),
            c.server.ttl,
            origins.join(", "),
            lit(&c.text),
            headers.join(", ")
        );
    }
    source += "}\n";
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let outcome = scalar::run(&program);
    assert_eq!(outcome.status, 0, "for {source}");
    let out = String::from_utf8(outcome.stdout).unwrap();
    let lines: Vec<&str> = out.lines().collect();
    assert_eq!(lines.len(), cases.len() * 3, "three lines for each case: {out}");
    lines
        .chunks(3)
        .map(|c| {
            assert_eq!(c[0], "--");
            Got { status: c[1].parse().unwrap(), text: (c[2] != "none").then(|| c[2].to_string()) }
        })
        .collect()
}

/// The names that the schema gives a definition: what it requires and what it has.
fn schema_names(definition: &str) -> (Vec<String>, Vec<String>) {
    static SCHEMA: OnceLock<Json> = OnceLock::new();
    let schema = SCHEMA.get_or_init(|| parse(&standard::read_under("mcp/2026-07-28/schema", "schema.json")));
    let Some(Json::Object(definitions)) = schema.get("$defs") else { panic!("the schema has definitions") };
    let Some((_, d)) = definitions.iter().find(|(k, _)| k == definition) else { panic!("{definition} is in the schema") };
    let required = match d.get("required") {
        Some(Json::Array(names)) => names.iter().filter_map(|n| n.text().map(str::to_string)).collect(),
        _ => Vec::new(),
    };
    let properties = d.get("properties").map(|p| p.members().iter().map(|s| s.to_string()).collect()).unwrap_or_default();
    (required, properties)
}

fn text_of(j: Option<&Json>) -> Option<&str> {
    j.and_then(Json::text)
}

fn number_of(j: Option<&Json>) -> Option<&str> {
    match j {
        Some(Json::Number(n)) => Some(n),
        _ => None,
    }
}

/// Holds the reply to what the case wants of it.
fn hold(case: &Case, got: &Got) {
    let context = format!("for {case:?}\n got {got:?}");
    match &case.want {
        Want::Silent { status } => {
            assert_eq!(got.status, *status, "{context}");
            assert_eq!(got.text, None, "{context}");
        }
        Want::Failed { status, code, id, data } => {
            assert_eq!(got.status, *status, "{context}");
            let text = got.text.as_ref().unwrap_or_else(|| panic!("a failure has a text {context}"));
            let reply = parse(text);
            let mut names = vec!["jsonrpc", "error"];
            assert_eq!(text_of(reply.get("jsonrpc")), Some("2.0"), "{context}");
            match id {
                Some(i) => {
                    names.push("id");
                    assert_eq!(reply.get("id"), Some(&parse(i)), "{context}");
                }
                None => assert!(reply.get("id").is_none(), "no id where none was read {context}"),
            }
            let mut members = reply.members();
            members.sort();
            names.sort();
            assert_eq!(members, names, "{context}");
            let error = reply.get("error").unwrap_or_else(|| panic!("{context}"));
            assert_eq!(number_of(error.get("code")), Some(code.to_string().as_str()), "{context}");
            assert!(text_of(error.get("message")).is_some_and(|m| !m.is_empty()), "{context}");
            match data {
                Some(d) => assert_eq!(error.get("data"), Some(&parse(d)), "{context}"),
                None => assert!(error.get("data").is_none(), "{context}"),
            }
        }
        Want::Discovered { id } => {
            assert_eq!(got.status, 200, "{context}");
            let text = got.text.as_ref().unwrap_or_else(|| panic!("a result has a text {context}"));
            let reply = parse(text);
            assert_eq!(text_of(reply.get("jsonrpc")), Some("2.0"), "{context}");
            assert_eq!(reply.get("id"), Some(&parse(id)), "{context}");
            assert!(reply.get("error").is_none(), "{context}");
            let result = reply.get("result").unwrap_or_else(|| panic!("{context}"));
            // Every member the schema requires, and none it does not name.
            let (required, properties) = schema_names("DiscoverResult");
            let members = result.members();
            for r in &required {
                assert!(members.contains(&r.as_str()), "the schema requires {r} {context}");
            }
            for m in &members {
                assert!(properties.iter().any(|p| p == m), "the schema has no {m} {context}");
            }
            assert_eq!(text_of(result.get("resultType")), Some("complete"), "{context}");
            assert_eq!(result.get("supportedVersions"), Some(&Json::Array(vec![Json::Text(VERSION.into())])), "{context}");
            assert_eq!(result.get("capabilities"), Some(&Json::Object(Vec::new())), "{context}");
            assert_eq!(number_of(result.get("ttlMs")), Some(case.server.ttl.as_str()), "{context}");
            assert_eq!(text_of(result.get("cacheScope")), Some("private"), "{context}");
            // `instructions` is there when the server has some, and left out, not `null`, when it has none.
            match &case.server.instructions {
                Some(i) => assert_eq!(text_of(result.get("instructions")), Some(i.as_str()), "{context}"),
                None => assert!(result.get("instructions").is_none(), "{context}"),
            }
            let info = result.get("_meta").and_then(|m| m.get(SERVER_INFO_KEY)).unwrap_or_else(|| panic!("the server's name {context}"));
            assert_eq!(text_of(info.get("name")), Some(case.server.name.as_str()), "{context}");
            assert_eq!(text_of(info.get("version")), Some(case.server.version.as_str()), "{context}");
        }
    }
}

fn hold_all(cases: &[Case]) {
    let got = replies(cases);
    for (c, g) in cases.iter().zip(&got) {
        hold(c, g);
    }
}

fn a_discover_request_is_answered_with_the_result_the_schema_names() {
    generate::check(
        5601,
        2,
        |g| (0..12).map(|_| discover_case(g)).collect::<Vec<Case>>(),
        |cases| hold_all(cases),
    );
}

fn a_request_with_a_version_or_a_meta_that_is_wrong_is_refused_with_the_failure_the_text_names() {
    generate::check(
        5602,
        2,
        |g| {
            let mut cases: Vec<Case> = Vec::new();
            for _ in 0..4 {
                cases.push(other_version_case(g));
                cases.push(initialize_case(g));
                cases.push(bad_meta_case(g));
                cases.push(array_params_case(g));
            }
            cases
        },
        |cases| hold_all(cases),
    );
}

fn a_method_the_server_has_not_is_answered_not_found_and_a_notification_by_none() {
    generate::check(
        5603,
        2,
        |g| {
            let mut cases: Vec<Case> = Vec::new();
            for _ in 0..6 {
                cases.push(unknown_method_case(g));
                cases.push(notification_case(g));
            }
            cases
        },
        |cases| hold_all(cases),
    );
}

/// A request that is right with one of its headers wrong or gone.
fn header_case(g: &mut Gen) -> Case {
    let id = id_text(g);
    let method = "server/discover";
    let meta = meta_text(g, VERSION);
    let text = request(g, &id, method, &meta, &[]);
    let mut headers = http_headers(g, VERSION, method);
    let wrong = g.below(5);
    match wrong {
        // No header of the version.
        0 => headers.retain(|(n, _)| n != "mcp-protocol-version"),
        // The version of another request.
        1 => headers.iter_mut().filter(|(n, _)| n == "mcp-protocol-version").for_each(|(_, v)| *v = "2025-11-25".into()),
        // No header of the method.
        2 => headers.retain(|(n, _)| n != "mcp-method"),
        // The method of another request.
        3 => headers.iter_mut().filter(|(n, _)| n == "mcp-method").for_each(|(_, v)| *v = "tools/call".into()),
        // Neither is wrong: the request is right.
        _ => {}
    }
    let want = if wrong < 4 { failed(400, HEADER_MISMATCH, Some(&id)) } else { Want::Discovered { id } };
    Case { server: spec(g), headers, text, want }
}

fn a_header_that_does_not_hold_to_the_message_is_refused_and_one_that_does_is_not() {
    generate::check(5604, 2, |g| (0..12).map(|_| header_case(g)).collect::<Vec<Case>>(), |cases| hold_all(cases));
}

/// The base64 of bytes, which is written here and not taken from the library.
fn base64(bytes: &[u8]) -> String {
    let alphabet = b"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    let mut out = String::new();
    for chunk in bytes.chunks(3) {
        let n = chunk.iter().enumerate().fold(0u32, |n, (i, b)| n | (*b as u32) << (16 - 8 * i));
        for i in 0..4 {
            if i <= chunk.len() {
                out.push(alphabet[(n >> (18 - 6 * i) & 63) as usize] as char);
            } else {
                out.push('=');
            }
        }
    }
    out
}

/// A request that names a tool, a prompt, or a resource, with the header that
/// names it written as it is, in base64, or wrong.
fn name_case(g: &mut Gen) -> Case {
    let id = id_text(g);
    let (method, key) = *g.pick(&[("tools/call", "name"), ("prompts/get", "name"), ("resources/read", "uri")]);
    let name = if g.one_in(3) { "=?base64?QQ==?=".to_string() } else { word(g) };
    let meta = meta_text(g, VERSION);
    let has_name = !g.one_in(6);
    let extra = if has_name { vec![(key.to_string(), json_string(&name))] } else { Vec::new() };
    let text = request(g, &id, method, &meta, &extra);
    let mut headers = http_headers(g, VERSION, method);
    let encoded = format!("=?base64?{}?=", base64(name.as_bytes()));
    // The header of the name, as written, as base64, or wrong.
    let (header, holds): (Option<String>, bool) = match g.below(6) {
        0 => (Some(encoded), true),
        1 => (Some(name.clone()), !name.starts_with("=?base64?")),
        2 => (Some(format!("{name}x")), false),
        3 => (None, false),
        4 => (Some("=?base64?!!?=".to_string()), false),
        _ => (Some(base64(name.as_bytes())), base64(name.as_bytes()) == name),
    };
    if let Some(h) = header {
        headers.push(("mcp-name".to_string(), h));
    }
    let want = if !has_name {
        failed(400, INVALID_PARAMS, Some(&id))
    } else if holds {
        // The server has no tools, no prompts, and no resources.
        failed(404, METHOD_NOT_FOUND, Some(&id))
    } else {
        failed(400, HEADER_MISMATCH, Some(&id))
    };
    Case { server: spec(g), headers, text, want }
}

fn a_name_in_a_header_is_held_to_the_name_in_the_message_as_written_or_in_base64() {
    generate::check(5605, 2, |g| (0..14).map(|_| name_case(g)).collect::<Vec<Case>>(), |cases| hold_all(cases));
}

fn a_text_that_is_no_request_is_refused_and_the_reply_has_no_id_where_none_was_read() {
    generate::check(
        5606,
        2,
        |g| {
            let mut cases: Vec<Case> = Vec::new();
            for _ in 0..10 {
                cases.push(no_request_case(g));
            }
            cases
        },
        |cases| hold_all(cases),
    );
}

/// A message that a page of a browser sent, or no page: its origin is in the
/// list of the server, or it is not, or the message has none.
fn origin_case(g: &mut Gen) -> Case {
    let mut c = if g.one_in(3) {
        no_request_case(g)
    } else {
        let mut c = discover_case(g);
        // A request that came over HTTP has the headers the protocol asks for.
        if c.headers.is_empty() {
            c.headers = http_headers(g, VERSION, "server/discover");
        }
        c
    };
    let origin = *g.pick(&["https://notes.example", "http://localhost:3000", "https://evil.example", "https://notes.example/", "HTTPS://NOTES.EXAMPLE"]);
    match g.below(3) {
        // A page of an origin that the server lists, or one that it does not.
        0 | 1 => {
            c.headers.push(("origin".to_string(), origin.to_string()));
            if !c.server.origins.iter().any(|o| o == origin) {
                c.want = Want::Silent { status: 403 };
            }
        }
        // No origin: the client is no browser.
        _ => c.headers.retain(|(n, _)| n != "origin"),
    }
    c
}

fn a_message_of_a_page_that_the_server_does_not_list_is_refused_before_it_is_read() {
    generate::check(5607, 2, |g| (0..14).map(|_| origin_case(g)).collect::<Vec<Case>>(), |cases| hold_all(cases));
}
