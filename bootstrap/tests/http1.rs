//! `std.http1` reads the heads and the chunks of HTTP/1.1 from bytes.
//!
//! The generator writes each message itself, as RFC 9112 says, and keeps what
//! it meant: the method, the target, the names and the values. A head is read
//! whole, in every front part of it, and with one fault put in that a server
//! must not accept, and the test says what the reader must say of each from
//! how the generator made it.

use smith::{lower, scalar};

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "the_example_of_the_page_gives_what_it_says_for_the_whole_head_and_for_one_byte_short",
        the_example_of_the_page_gives_what_it_says_for_the_whole_head_and_for_one_byte_short,
    ),
    (
        "a_head_that_is_written_is_read_back_and_every_front_part_of_it_is_incomplete",
        a_head_that_is_written_is_read_back_and_every_front_part_of_it_is_incomplete,
    ),
    (
        "a_head_with_one_of_the_faults_of_the_page_put_in_is_refused_and_never_complete",
        a_head_with_one_of_the_faults_of_the_page_put_in_is_refused_and_never_complete,
    ),
    (
        "a_head_longer_than_the_limit_is_refused_at_the_limit_however_the_bytes_go_on",
        a_head_longer_than_the_limit_is_refused_at_the_limit_however_the_bytes_go_on,
    ),
    (
        "body_of_says_how_the_body_is_framed_from_the_headers",
        body_of_says_how_the_body_is_framed_from_the_headers,
    ),
    (
        "a_chunk_is_read_as_its_bytes_with_what_it_took_and_a_chunk_of_no_bytes_is_the_last",
        a_chunk_is_read_as_its_bytes_with_what_it_took_and_a_chunk_of_no_bytes_is_the_last,
    ),
    (
        "write_chunk_writes_a_chunk_that_chunk_reads_back",
        write_chunk_writes_a_chunk_that_chunk_reads_back,
    ),
];

const CRLF: &[u8] = b"\r\n";

/// A head the generator means: its first line, and the name and the value of
/// each header as they are meant to be read, with the spaces round the value
/// it was written with.
#[derive(Clone, Debug)]
struct Head {
    first: String,
    headers: Vec<(String, String, String, String)>,
    /// What a reader must give: the method and the target of a request, or
    /// the status and the reason of a response.
    parts: (String, String),
    request: bool,
}

fn word(g: &mut Gen, letters: &[char], most: usize) -> String {
    (0..1 + g.below(most)).map(|_| *g.pick(letters)).collect()
}

fn blanks(g: &mut Gen) -> String {
    (0..g.below(3)).map(|_| *g.pick(&[' ', '\t'])).collect()
}

fn head(g: &mut Gen) -> Head {
    let request = g.one_in(2);
    let (first, parts) = if request {
        let method = (*g.pick(&["GET", "POST", "PUT", "DELETE", "x-Custom"])).to_string();
        let target = format!("/{}", word(g, &['a', 'b', '0', '/', '?', '=', '&', '%'], 6));
        (format!("{method} {target} HTTP/1.1"), (method, target))
    } else {
        let status = 100 + g.below(500);
        let reason = if g.one_in(3) {
            String::new()
        } else {
            word(g, &['O', 'k', ' ', 'é', 'N'], 6)
        };
        (format!("HTTP/1.1 {status} {reason}"), (status.to_string(), reason))
    };
    let headers = (0..g.below(6))
        .map(|_| {
            let name = word(g, &['a', 'B', 'x', '-', '_', '9', 'Z'], 6);
            let core = if g.one_in(4) {
                String::new()
            } else {
                // The first and the last of a value are not blank.
                let mut core = word(g, &['a', 'Z', '0', '=', ';', '/', 'é', '語'], 1);
                core += &word(g, &['a', ' ', '\t', ';', 'é'], 5);
                core.push(*g.pick(&['a', '1', '語']));
                core
            };
            let (before, after) = (blanks(g), blanks(g));
            (name.to_ascii_lowercase(), name, core, format!("{before}\u{0}{after}"))
        })
        .collect();
    Head { first, headers, parts, request }
}

/// The bytes of a head: each line ends with a carriage return and a line feed.
fn written(h: &Head) -> Vec<u8> {
    let mut bytes = h.first.clone().into_bytes();
    bytes.extend(CRLF);
    for (_, name, core, around) in &h.headers {
        let (before, after) = around.split_once('\u{0}').unwrap();
        bytes.extend(format!("{name}:{before}{core}{after}").bytes());
        bytes.extend(CRLF);
    }
    bytes.extend(CRLF);
    bytes
}

fn list(bytes: &[u8]) -> String {
    let items: Vec<String> = bytes.iter().map(u8::to_string).collect();
    format!("[{}]", items.join(", "))
}

const SHOW: &str = "fn show_request(b: take List[Byte]) ! console, alloc {\n  match request_head(freeze(b)) {\n    .complete((h, used)) {\n      print(\"request {h.method} {h.target} {used}\")\n      loop kv in h.headers {\n        match kv {\n          (n, v) { print(\"{n}|{v}|\") }\n        }\n      }\n    }\n    .incomplete { print(\"incomplete\") }\n    .refused(why) { print(\"refused\") }\n  }\n}\n\nfn show_response(b: take List[Byte]) ! console, alloc {\n  match response_head(freeze(b)) {\n    .complete((h, used)) {\n      print(\"response {h.status} {h.reason}| {used}\")\n      loop kv in h.headers {\n        match kv {\n          (n, v) { print(\"{n}|{v}|\") }\n        }\n      }\n    }\n    .incomplete { print(\"incomplete\") }\n    .refused(why) { print(\"refused\") }\n  }\n}\n\n";

/// A program that runs each of `calls` and prints what each gives.
fn program(imports: &str, helpers: &str, calls: &[String]) -> String {
    let body: String = calls.iter().map(|c| format!("  {c}\n")).collect();
    format!(
        "use std.console (console, print)\nuse std.http1 ({imports})\nuse std.list (freeze)\n\n{helpers}fn main() ! console, alloc {{\n{body}}}\n"
    )
}

fn printed(source: &str) -> Vec<String> {
    let lowered = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let outcome = scalar::run(&lowered);
    assert_eq!(outcome.status, 0, "for {source}");
    String::from_utf8(outcome.stdout)
        .unwrap()
        .lines()
        .map(str::to_string)
        .collect()
}

fn read_program(calls: &[String]) -> String {
    program("request_head, response_head", SHOW, calls)
}

/// What `show_request` or `show_response` prints for a head that is read whole.
fn expected(h: &Head) -> Vec<String> {
    let used = written(h).len();
    let mut lines = vec![if h.request {
        format!("request {} {} {used}", h.parts.0, h.parts.1)
    } else {
        format!("response {} {}| {used}", h.parts.0, h.parts.1)
    }];
    for (lower, _, core, _) in &h.headers {
        lines.push(format!("{lower}|{core}|"));
    }
    lines
}

fn call(h: &Head, bytes: &[u8]) -> String {
    format!("{}({})", if h.request { "show_request" } else { "show_response" }, list(bytes))
}

fn the_example_of_the_page_gives_what_it_says_for_the_whole_head_and_for_one_byte_short() {
    let helpers = "fn first_line(bytes: Slice[Byte]) -> str ! alloc {\n  match request_head(bytes) {\n    .complete((head, used)) { \"{head.method} {head.target}\" }\n    .incomplete { \"more bytes are needed\" }\n    .refused(why) { why }\n  }\n}\n\nfn say(b: take List[Byte]) ! console, alloc {\n  print(first_line(freeze(b)))\n}\n\n";
    let whole = b"GET /notes?limit=3 HTTP/1.1\r\nhost: example.com\r\n\r\n";
    let source = program(
        "Read, request_head",
        helpers,
        &[
            format!("say({})", list(whole)),
            format!("say({})", list(&whole[..whole.len() - 1])),
        ],
    );
    assert_eq!(
        printed(&source),
        vec!["GET /notes?limit=3", "more bytes are needed"]
    );
}

fn a_head_that_is_written_is_read_back_and_every_front_part_of_it_is_incomplete() {
    generate::check(
        5951,
        3,
        |g| {
            (0..14)
                .map(|_| {
                    let h = head(g);
                    let cuts: Vec<usize> = (0..3).map(|_| g.below(written(&h).len())).collect();
                    (h, cuts)
                })
                .collect::<Vec<_>>()
        },
        |batch| {
            let mut calls = Vec::new();
            let mut lines = Vec::new();
            for (h, cuts) in batch {
                let bytes = written(h);
                calls.push(call(h, &bytes));
                lines.extend(expected(h));
                // The head one byte short, and parts of it at the places chosen.
                for cut in std::iter::once(bytes.len() - 1).chain(cuts.iter().copied()) {
                    calls.push(call(h, &bytes[..cut]));
                    lines.push("incomplete".to_string());
                }
                // Bytes after the head are not read and are not counted.
                let mut more = bytes.clone();
                more.extend(b"GET / HTTP/1.1\r\n");
                calls.push(call(h, &more));
                lines.extend(expected(h));
            }
            assert_eq!(printed(&read_program(&calls)), lines);
        },
    );
}

/// One fault of the page's list, put into the lines of a head that has at
/// least one header.
fn fault(g: &mut Gen, h: &Head) -> Vec<u8> {
    let mut lines: Vec<Vec<u8>> = vec![h.first.clone().into_bytes()];
    for (_, name, core, around) in &h.headers {
        let (before, after) = around.split_once('\u{0}').unwrap();
        lines.push(format!("{name}:{before}{core}{after}").into_bytes());
    }
    let mut ends: Vec<&[u8]> = vec![CRLF; lines.len()];
    let at = 1 + g.below(lines.len() - 1);
    match g.below(8) {
        // A line that ends with a line feed alone.
        0 => ends[at] = b"\n",
        // A carriage return that no line feed follows.
        1 => ends[at] = b"\rX\n",
        // A space before the colon.
        2 => {
            let colon = lines[at].iter().position(|b| *b == b':').unwrap();
            lines[at].insert(colon, *g.pick(b" \t"));
        }
        // A header line that continues the one before.
        3 => lines.insert(at, format!("{}x", *g.pick(&[" ", "\t"])).into_bytes()),
        // A byte that is no part of a value.
        4 => {
            let colon = lines[at].iter().position(|b| *b == b':').unwrap();
            lines[at].insert(colon + 1, *g.pick(&[0, 1, 31, 127]));
        }
        // More than one content-length.
        5 => {
            lines.insert(at, b"content-length: 1".to_vec());
            lines.insert(at, b"Content-Length: 1".to_vec());
        }
        // A content-length that is not a number.
        6 => lines.insert(at, format!("content-length: {}", g.pick(&["a", "1a", "-1", "1 2", "", "99999999999999999999999"])).into_bytes()),
        // A content-length together with a transfer-encoding.
        _ => {
            lines.insert(at, b"transfer-encoding: chunked".to_vec());
            lines.insert(at, b"content-length: 3".to_vec());
        }
    }
    ends.resize(lines.len(), CRLF);
    let mut bytes = Vec::new();
    for (line, end) in lines.iter().zip(&ends) {
        bytes.extend(line);
        bytes.extend(*end);
    }
    bytes.extend(CRLF);
    bytes
}

fn a_head_with_one_of_the_faults_of_the_page_put_in_is_refused_and_never_complete() {
    generate::check(
        5952,
        3,
        |g| {
            (0..30)
                .map(|_| {
                    let mut h = head(g);
                    if h.headers.is_empty() {
                        h.headers.push(("x".into(), "x".into(), "1".into(), "\u{0}".into()));
                    }
                    let bytes = fault(g, &h);
                    (h, bytes)
                })
                .collect::<Vec<_>>()
        },
        |batch| {
            let calls: Vec<String> = batch.iter().map(|(h, bytes)| call(h, bytes)).collect();
            assert_eq!(printed(&read_program(&calls)), vec!["refused"; batch.len()]);
        },
    );
}

fn a_head_longer_than_the_limit_is_refused_at_the_limit_however_the_bytes_go_on() {
    // `limit` is the second argument of each reader, and 16384 when left out.
    generate::check(
        5953,
        3,
        |g| {
            (0..10)
                .map(|_| {
                    let h = head(g);
                    let size = written(&h).len();
                    // A limit that the head fits, and one it does not.
                    let fits = size + g.below(4);
                    let short = 1 + g.below(size - 1);
                    let tail: Vec<u8> = (0..g.below(40)).map(|_| *g.pick(b"ab: ")).collect();
                    (h, fits, short, tail)
                })
                .collect::<Vec<_>>()
        },
        |batch| {
            let mut calls = Vec::new();
            let mut lines = Vec::new();
            for (h, fits, short, tail) in batch {
                let bytes = written(h);
                let show = |bytes: &[u8], limit: usize| {
                    let body = if h.request {
                        "show_request_limited"
                    } else {
                        "show_response_limited"
                    };
                    format!("{body}({}, {limit})", list(bytes))
                };
                calls.push(show(&bytes, *fits));
                lines.extend(expected(h));
                // The same bytes and a limit below their size, however they go on.
                let mut longer = bytes.clone();
                longer.extend(tail);
                for given in [&bytes[..], &bytes[..*short], &longer[..]] {
                    if given.len() >= *short {
                        calls.push(show(given, *short));
                        lines.push("refused".to_string());
                    }
                }
            }
            let helpers = SHOW
                .replace("show_request(b: take List[Byte])", "show_request_limited(b: take List[Byte], limit: Nat)")
                .replace("show_response(b: take List[Byte])", "show_response_limited(b: take List[Byte], limit: Nat)")
                .replace("request_head(freeze(b))", "request_head(freeze(b), limit)")
                .replace("response_head(freeze(b))", "response_head(freeze(b), limit)");
            let source = program("request_head, response_head", &helpers, &calls);
            assert_eq!(printed(&source), lines);
        },
    );
}

fn body_of_says_how_the_body_is_framed_from_the_headers() {
    let helpers = "fn show(h: List[(str, str)]) ! console, alloc {\n  match body_of(h) {\n    .ok(b) {\n      match b {\n        .none { print(\"none\") }\n        .length(n) { print(\"length {n}\") }\n        .chunked { print(\"chunked\") }\n      }\n    }\n    .err(why) { print(\"err\") }\n  }\n}\n\n";
    let cases: Vec<(&str, String)> = vec![
        ("[]", "none".into()),
        ("[(\"host\", \"a\")]", "none".into()),
        ("[(\"content-length\", \"0\")]", "length 0".into()),
        ("[(\"host\", \"a\"), (\"content-length\", \"1234\")]", "length 1234".into()),
        ("[(\"transfer-encoding\", \"chunked\")]", "chunked".into()),
        ("[(\"transfer-encoding\", \"gzip, chunked\")]", "chunked".into()),
        ("[(\"transfer-encoding\", \"Chunked\")]", "chunked".into()),
        ("[(\"transfer-encoding\", \"chunked, gzip\")]", "err".into()),
        ("[(\"transfer-encoding\", \"notchunked\")]", "err".into()),
        ("[(\"content-length\", \"1\"), (\"content-length\", \"1\")]", "err".into()),
        ("[(\"content-length\", \"x\")]", "err".into()),
        ("[(\"content-length\", \"18446744073709551616\")]", "err".into()),
        ("[(\"content-length\", \"1\"), (\"transfer-encoding\", \"chunked\")]", "err".into()),
    ];
    let calls: Vec<String> = cases.iter().map(|(h, _)| format!("show({h})")).collect();
    let source = program("body_of", helpers, &calls);
    let lines: Vec<String> = cases.into_iter().map(|(_, l)| l).collect();
    assert_eq!(printed(&source), lines);
}

/// What `chunk` is read as, in the helper of the programs below.
const CHUNK: &str = "fn show_chunk(b: take List[Byte]) ! console, alloc {\n  match chunk(freeze(b)) {\n    .complete((bytes, used)) {\n      print(\"chunk {bytes.size} {used}\")\n      loop v in bytes { print(\"{v}\") }\n    }\n    .incomplete { print(\"incomplete\") }\n    .refused(why) { print(\"refused\") }\n  }\n}\n\n";

/// A chunk of the bytes, written as RFC 9112 says, with the extension and
/// the hexadecimal letters the generator chose.
fn written_chunk(g: &mut Gen, data: &[u8]) -> Vec<u8> {
    let size = if g.one_in(2) {
        format!("{:x}", data.len())
    } else {
        format!("{:X}", data.len())
    };
    let extension = if g.one_in(3) { ";name=value" } else { "" };
    let mut bytes = format!("{size}{extension}\r\n").into_bytes();
    bytes.extend(data);
    bytes.extend(CRLF);
    bytes
}

fn data_of(g: &mut Gen) -> Vec<u8> {
    // The data holds line breaks, as any bytes do.
    (0..g.below(24)).map(|_| *g.pick(&[0u8, 10, 13, 58, 65, 255])).collect()
}

fn a_chunk_is_read_as_its_bytes_with_what_it_took_and_a_chunk_of_no_bytes_is_the_last() {
    generate::check(
        5954,
        3,
        |g| {
            (0..16)
                .map(|_| {
                    let data = data_of(g);
                    let bytes = written_chunk(g, &data);
                    let cut = g.below(bytes.len());
                    (data, bytes, cut, g.one_in(3))
                })
                .collect::<Vec<_>>()
        },
        |batch| {
            let mut calls = Vec::new();
            let mut lines = Vec::new();
            for (data, bytes, cut, trailers) in batch {
                let mut given = bytes.clone();
                let mut used = bytes.len();
                if data.is_empty() && *trailers {
                    // Trailers after the last chunk are passed over.
                    given = b"0\r\nx-sum: 1\r\ny: 2\r\n\r\n".to_vec();
                    used = given.len();
                }
                let mut more = given.clone();
                more.extend(b"5\r\nhello\r\n");
                calls.push(format!("show_chunk({})", list(&more)));
                lines.push(format!("chunk {} {used}", data.len()));
                lines.extend(data.iter().map(u8::to_string));
                // Any front part is incomplete.
                calls.push(format!("show_chunk({})", list(&given[..*cut.min(&(given.len() - 1))])));
                lines.push("incomplete".to_string());
            }
            assert_eq!(printed(&program("chunk", CHUNK, &calls)), lines);
        },
    );
    // What is refused.
    let refused: Vec<&[u8]> = vec![
        b"x\r\n",
        b"\r\n",
        b"5\nhello\r\n",
        b"5\r\nhelloXX",
        b"5;a\nhello\r\n",
        b"ffffffffffffffffffff\r\n",
        b"5 \r\nhello\r\n",
        b"0\r\nx\ny\r\n\r\n",
    ];
    let calls: Vec<String> = refused.iter().map(|b| format!("show_chunk({})", list(b))).collect();
    assert_eq!(
        printed(&program("chunk", CHUNK, &calls)),
        vec!["refused"; refused.len()]
    );
}

fn write_chunk_writes_a_chunk_that_chunk_reads_back() {
    let helpers = format!("{CHUNK}fn show_written(data: Slice[Byte]) ! console, alloc {{\n  empty: List[Byte] = []\n  w = write_chunk(empty, data)\n  loop v in w {{ print(\"{{v}}\") }}\n  show_chunk(w)\n}}\n\n");
    generate::check(
        5955,
        3,
        |g| (0..12).map(|_| data_of(g)).collect::<Vec<_>>(),
        |batch| {
            let calls: Vec<String> = batch
                .iter()
                .map(|d| format!("show_written(freeze({}))", list(d)))
                .collect();
            let mut lines = Vec::new();
            for data in batch {
                let mut bytes = format!("{:x}\r\n", data.len()).into_bytes();
                bytes.extend(data);
                bytes.extend(CRLF);
                lines.extend(bytes.iter().map(u8::to_string));
                lines.push(format!("chunk {} {}", data.len(), bytes.len()));
                lines.extend(data.iter().map(u8::to_string));
            }
            assert_eq!(printed(&program("chunk, write_chunk", &helpers, &calls)), lines);
        },
    );
}
