//! `std.http2` reads and writes the frames of HTTP/2 and the headers of
//! HPACK, as `knowledge/library/http2.md` says.
//!
//! The expectation of each test comes from here and not from the module: the
//! bytes of a frame are laid out by `F::bytes`, a block of headers by
//! `rfc`, which reads the text of the standard, or by the walker of HPACK in this file, and a program built
//! from the source of the test is run by the evaluator and prints what the
//! module gave.

use smith::{lower, scalar};

use crate::Test;
use crate::generate::{self, Gen};
use crate::rfc;

pub const ONCE: &[Test] = &[
    (
        "a_frame_is_written_as_its_bytes_and_read_from_them_and_a_front_part_of_it_is_incomplete",
        a_frame_is_written_as_its_bytes_and_read_from_them_and_a_front_part_of_it_is_incomplete,
    ),
    (
        "a_frame_with_one_fault_the_page_lists_is_refused_and_without_it_is_read",
        a_frame_with_one_fault_the_page_lists_is_refused_and_without_it_is_read,
    ),
    (
        "bytes_of_any_kind_are_read_as_a_frame_or_refused_and_never_end_the_program",
        bytes_of_any_kind_are_read_as_a_frame_or_refused_and_never_end_the_program,
    ),
    (
        "the_frame_types_settings_and_error_codes_of_rfc_9113_are_read_and_written_as_the_numbers_it_gives",
        the_frame_types_settings_and_error_codes_of_rfc_9113_are_read_and_written_as_the_numbers_it_gives,
    ),
    (
        "the_examples_of_rfc_7541_appendix_c_decode_to_the_headers_and_the_table_it_gives",
        the_examples_of_rfc_7541_appendix_c_decode_to_the_headers_and_the_table_it_gives,
    ),
    (
        "the_headers_of_appendix_c_are_encoded_to_blocks_that_read_back_and_to_the_bytes_of_the_appendix_where_the_page_allows",
        the_headers_of_appendix_c_are_encoded_to_blocks_that_read_back_and_to_the_bytes_of_the_appendix_where_the_page_allows,
    ),
    (
        "the_static_table_is_the_one_of_rfc_7541_appendix_a",
        the_static_table_is_the_one_of_rfc_7541_appendix_a,
    ),
    (
        "the_numbers_of_rfc_7541_appendix_c_1_are_read_from_their_bytes",
        the_numbers_of_rfc_7541_appendix_c_1_are_read_from_their_bytes,
    ),
    (
        "the_short_code_of_rfc_7541_appendix_b_is_read_and_written_for_every_byte",
        the_short_code_of_rfc_7541_appendix_b_is_read_and_written_for_every_byte,
    ),
    (
        "decoding_what_was_encoded_gives_the_headers_over_blocks_with_one_encoder_and_one_decoder",
        decoding_what_was_encoded_gives_the_headers_over_blocks_with_one_encoder_and_one_decoder,
    ),
    (
        "a_block_is_an_error_where_the_page_says_and_a_block_without_that_fault_is_read",
        a_block_is_an_error_where_the_page_says_and_a_block_without_that_fault_is_read,
    ),
    (
        "a_text_in_the_short_code_is_read_and_one_with_wrong_padding_or_its_end_is_an_error",
        a_text_in_the_short_code_is_read_and_one_with_wrong_padding_or_its_end_is_an_error,
    ),
    (
        "a_block_of_more_than_largest_bytes_is_an_error_and_of_largest_or_fewer_is_read",
        a_block_of_more_than_largest_bytes_is_an_error_and_of_largest_or_fewer_is_read,
    ),
    (
        "a_block_that_comes_to_more_than_largest_bytes_is_an_error_before_that_much_memory_is_asked_for",
        a_block_that_comes_to_more_than_largest_bytes_is_an_error_before_that_much_memory_is_asked_for,
    ),
    (
        "a_header_named_authorization_cookie_or_set_cookie_is_written_never_indexed_and_learned_by_no_one",
        a_header_named_authorization_cookie_or_set_cookie_is_written_never_indexed_and_learned_by_no_one,
    ),
    (
        "blocks_of_any_bytes_are_read_or_an_error_and_never_end_the_program",
        blocks_of_any_bytes_are_read_or_an_error_and_never_end_the_program,
    ),
];

/// What a program prints, as lines. A program that is refused or that ends in
/// error is a wrong test, which fails it.
fn lines(source: &str) -> Vec<String> {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{source}"));
    let outcome = scalar::run(&program);
    assert_eq!(outcome.status, 0, "the program ended in error\n{source}");
    let text = String::from_utf8(outcome.stdout).expect("text");
    text.lines().map(str::to_string).collect()
}

/// A run of bytes, written as a list of numbers in source.
fn list(bytes: &[u8]) -> String {
    let numbers: Vec<String> = bytes.iter().map(|b| b.to_string()).collect();
    format!("[{}]", numbers.join(", "))
}

fn unhex(text: &str) -> Vec<u8> {
    (0..text.len())
        .step_by(2)
        .map(|i| u8::from_str_radix(&text[i..i + 2], 16).expect("hex"))
        .collect()
}

/// A text as a string literal of source.
fn quoted(s: &str) -> String {
    format!(
        "\"{}\"",
        s.replace('\\', "\\\\").replace('"', "\\\"").replace('{', "\\{").replace('}', "\\}")
    )
}

// The frames.

/// A frame as the standard lays it out, kept as what each field holds.
#[derive(Clone, Debug)]
enum F {
    Data { stream: u32, end_stream: bool, padding: Option<u8>, data: Vec<u8> },
    Headers {
        stream: u32,
        end_stream: bool,
        end_headers: bool,
        padding: Option<u8>,
        priority: Option<(bool, u32, u8)>,
        block: Vec<u8>,
    },
    Priority(u32),
    Reset { stream: u32, code: u32 },
    Settings(Vec<(u32, u32)>),
    SettingsAck,
    Promise { stream: u32, end_headers: bool, padding: Option<u8>, promised: u32, block: Vec<u8> },
    Ping { ack: bool, data: u64 },
    GoAway { last: u32, code: u32, debug: Vec<u8> },
    Window { stream: u32, increment: u32 },
    Continuation { stream: u32, end_headers: bool, block: Vec<u8> },
    Unknown(u8),
}

fn be(n: u64, bytes: usize) -> Vec<u8> {
    (0..bytes).rev().map(|i| (n >> (8 * i)) as u8).collect()
}

impl F {
    fn kind(&self) -> u8 {
        match self {
            F::Data { .. } => 0,
            F::Headers { .. } => 1,
            F::Priority(_) => 2,
            F::Reset { .. } => 3,
            F::Settings(_) | F::SettingsAck => 4,
            F::Promise { .. } => 5,
            F::Ping { .. } => 6,
            F::GoAway { .. } => 7,
            F::Window { .. } => 8,
            F::Continuation { .. } => 9,
            F::Unknown(k) => *k,
        }
    }

    fn stream(&self) -> u32 {
        match self {
            F::Data { stream, .. }
            | F::Headers { stream, .. }
            | F::Reset { stream, .. }
            | F::Promise { stream, .. }
            | F::Window { stream, .. }
            | F::Continuation { stream, .. } => *stream,
            F::Priority(stream) => *stream,
            _ => 0,
        }
    }

    fn flags(&self) -> u8 {
        let on = |b: bool, bit: u8| if b { bit } else { 0 };
        match self {
            F::Data { end_stream, padding, .. } => on(*end_stream, 1) | on(padding.is_some(), 8),
            F::Headers { end_stream, end_headers, padding, priority, .. } => {
                on(*end_stream, 1) | on(*end_headers, 4) | on(padding.is_some(), 8) | on(priority.is_some(), 32)
            }
            F::SettingsAck => 1,
            F::Promise { end_headers, padding, .. } => on(*end_headers, 4) | on(padding.is_some(), 8),
            F::Ping { ack, .. } => on(*ack, 1),
            F::Continuation { end_headers, .. } => on(*end_headers, 4),
            _ => 0,
        }
    }

    /// The flags the standard gives a meaning to in a frame of this kind.
    fn used_flags(&self) -> u8 {
        match self {
            F::Data { .. } => 1 | 8,
            F::Headers { .. } => 1 | 4 | 8 | 32,
            F::Settings(_) | F::SettingsAck | F::Ping { .. } => 1,
            F::Promise { .. } => 4 | 8,
            F::Continuation { .. } => 4,
            F::Unknown(_) => 0,
            _ => 0,
        }
    }

    /// The bytes of the frame after its nine: the padding is of zeros.
    fn payload(&self) -> Vec<u8> {
        let pad = |padding: &Option<u8>| padding.map(|p| vec![p]).unwrap_or_default();
        let zeros = |padding: &Option<u8>| vec![0; padding.unwrap_or(0) as usize];
        match self {
            F::Data { padding, data, .. } => [pad(padding), data.clone(), zeros(padding)].concat(),
            F::Headers { padding, priority, block, .. } => {
                let dependency = priority
                    .map(|(exclusive, stream, weight)| {
                        [be(u64::from(stream) | if exclusive { 1 << 31 } else { 0 }, 4), vec![weight]].concat()
                    })
                    .unwrap_or_default();
                [pad(padding), dependency, block.clone(), zeros(padding)].concat()
            }
            F::Priority(_) => vec![0; 5],
            F::Reset { code, .. } => be(u64::from(*code), 4),
            F::Settings(pairs) => pairs
                .iter()
                .flat_map(|(id, value)| [be(u64::from(*id), 2), be(u64::from(*value), 4)].concat())
                .collect(),
            F::SettingsAck | F::Unknown(_) => Vec::new(),
            F::Promise { padding, promised, block, .. } => {
                [pad(padding), be(u64::from(*promised), 4), block.clone(), zeros(padding)].concat()
            }
            F::Ping { data, .. } => be(*data, 8),
            F::GoAway { last, code, debug } => {
                [be(u64::from(*last), 4), be(u64::from(*code), 4), debug.clone()].concat()
            }
            F::Window { increment, .. } => be(u64::from(*increment), 4),
            F::Continuation { block, .. } => block.clone(),
        }
    }

    fn bytes(&self) -> Vec<u8> {
        let payload = self.payload();
        [
            be(payload.len() as u64, 3),
            vec![self.kind(), self.flags()],
            be(u64::from(self.stream()), 4),
            payload,
        ]
        .concat()
    }

    /// What `describe` of the program prints for the frame.
    fn describe(&self) -> Vec<String> {
        let pad = |p: &Option<u8>| p.map(|p| p as i32).unwrap_or(-1);
        let bytes = |b: &[u8]| {
            let mut lines = vec![b.len().to_string()];
            lines.extend(b.iter().map(|x| x.to_string()));
            lines
        };
        let mut out = Vec::new();
        match self {
            F::Data { stream, end_stream, padding, data } => {
                out.push(format!("data {stream} {end_stream} {}", pad(padding)));
                out.extend(bytes(data));
            }
            F::Headers { stream, end_stream, end_headers, padding, priority, block } => {
                out.push(format!("headers {stream} {end_stream} {end_headers} {}", pad(padding)));
                match priority {
                    Some((exclusive, on, weight)) => out.push(format!("dependency {exclusive} {on} {weight}")),
                    None => out.push("independent".to_string()),
                }
                out.extend(bytes(block));
            }
            F::Priority(stream) => out.push(format!("priority {stream}")),
            F::Reset { stream, code } => out.push(format!("reset {stream} {code}")),
            F::Settings(pairs) => {
                out.push(format!("settings {}", pairs.len()));
                out.extend(pairs.iter().map(|(id, value)| format!("{id} {value}")));
            }
            F::SettingsAck => out.push("settings_ack".to_string()),
            F::Promise { stream, end_headers, padding, promised, block } => {
                out.push(format!("promise {stream} {end_headers} {} {promised}", pad(padding)));
                out.extend(bytes(block));
            }
            F::Ping { ack, data } => out.push(format!("ping {ack} {data}")),
            F::GoAway { last, code, debug } => {
                out.push(format!("go_away {last} {code}"));
                out.extend(bytes(debug));
            }
            F::Window { stream, increment } => out.push(format!("window {stream} {increment}")),
            F::Continuation { stream, end_headers, block } => {
                out.push(format!("continuation {stream} {end_headers}"));
                out.extend(bytes(block));
            }
            F::Unknown(kind) => out.push(format!("unknown {kind}")),
        }
        out
    }

    /// The frame as an expression of the program.
    fn source(&self) -> String {
        let pad = |p: &Option<u8>| match p {
            Some(p) => format!(".some({p})"),
            None => ".none".to_string(),
        };
        match self {
            F::Data { stream, end_stream, padding, data } => format!(
                "Frame.data(Data(stream: {stream}, end_stream: {end_stream}, padding: {}, data: {}))",
                pad(padding),
                list(data)
            ),
            F::Headers { stream, end_stream, end_headers, padding, priority, block } => {
                let dependency = match priority {
                    Some((exclusive, on, weight)) => {
                        format!(".some(Dependency(exclusive: {exclusive}, stream: {on}, weight: {weight}))")
                    }
                    None => ".none".to_string(),
                };
                format!(
                    "Frame.headers(Headers(stream: {stream}, end_stream: {end_stream}, end_headers: {end_headers}, padding: {}, priority: {dependency}, block: {}))",
                    pad(padding),
                    list(block)
                )
            }
            F::Priority(stream) => format!("Frame.priority({stream})"),
            F::Reset { stream, code } => format!("Frame.reset(Reset(stream: {stream}, code: {code}))"),
            F::Settings(pairs) => {
                let items: Vec<String> = pairs.iter().map(|(a, b)| format!("({a}, {b})")).collect();
                format!("Frame.settings([{}])", items.join(", "))
            }
            F::SettingsAck => "Frame.settings_ack".to_string(),
            F::Promise { stream, end_headers, padding, promised, block } => format!(
                "Frame.push_promise(Promise(stream: {stream}, end_headers: {end_headers}, padding: {}, promised: {promised}, block: {}))",
                pad(padding),
                list(block)
            ),
            F::Ping { ack, data } => format!("Frame.ping(Ping(ack: {ack}, data: {data}))"),
            F::GoAway { last, code, debug } => {
                format!("Frame.go_away(GoAway(last: {last}, code: {code}, debug: {}))", list(debug))
            }
            F::Window { stream, increment } => {
                format!("Frame.window_update(Window(stream: {stream}, increment: {increment}))")
            }
            F::Continuation { stream, end_headers, block } => format!(
                "Frame.continuation(Continuation(stream: {stream}, end_headers: {end_headers}, block: {}))",
                list(block)
            ),
            F::Unknown(kind) => format!("Frame.unknown({kind})"),
        }
    }
}

fn some_bytes(g: &mut Gen, most: usize) -> Vec<u8> {
    (0..g.below(most + 1)).map(|_| g.below(256) as u8).collect()
}

fn stream(g: &mut Gen) -> u32 {
    *g.pick(&[1, 2, 3, 7, 255, 65536, 2147483647, 1000000])
}

fn padding(g: &mut Gen) -> Option<u8> {
    if g.one_in(2) { Some(*g.pick(&[0, 1, 2, 5, 40, 255])) } else { None }
}

/// A frame of any kind, with the numbers a frame holds in their ranges.
fn frame(g: &mut Gen) -> F {
    match g.below(12) {
        0 => F::Data { stream: stream(g), end_stream: g.one_in(2), padding: padding(g), data: some_bytes(g, 12) },
        1 => F::Headers {
            stream: stream(g),
            end_stream: g.one_in(2),
            end_headers: g.one_in(2),
            padding: padding(g),
            priority: if g.one_in(2) { Some((g.one_in(2), stream(g), g.below(256) as u8)) } else { None },
            block: some_bytes(g, 12),
        },
        2 => F::Priority(stream(g)),
        3 => F::Reset { stream: stream(g), code: *g.pick(&[0, 1, 8, 4294967295, 65536]) },
        4 => F::Settings(
            (0..g.below(4))
                .map(|_| (*g.pick(&[1, 2, 3, 4, 5, 6, 65535, 7]), *g.pick(&[0, 1, 100, 65535, 4294967295])))
                .collect(),
        ),
        5 => F::SettingsAck,
        6 => F::Promise {
            stream: stream(g),
            end_headers: g.one_in(2),
            padding: padding(g),
            promised: stream(g),
            block: some_bytes(g, 12),
        },
        7 => F::Ping { ack: g.one_in(2), data: *g.pick(&[0, 1, 258, u64::MAX, 1 << 40]) },
        8 => F::GoAway { last: stream(g), code: *g.pick(&[0, 2, 4294967295]), debug: some_bytes(g, 10) },
        9 => F::Window { stream: *g.pick(&[0, 1, 2147483647]), increment: *g.pick(&[0, 1, 65535, 2147483647]) },
        10 => F::Continuation { stream: stream(g), end_headers: g.one_in(2), block: some_bytes(g, 12) },
        _ => F::Unknown(10 + g.below(246) as u8),
    }
}

/// The start of every program that reads frames: how a frame is printed.
const FRAMES: &str = "use std.console (console, print)
use std.http2 (Continuation, Data, Dependency, Frame, GoAway, Headers, Ping, Promise, Read, Reset, Window, frame, write_frame)
use std.int (wrapping_add)
use std.list (freeze)

fn pad(p: Option[Byte]) -> Int {
  match p {
    .some(x) { convert(x) }
    .none { -1 }
  }
}

fn bytes_of(b: Slice[Byte]) ! console, alloc {
  print(\"{b.size}\")
  loop x in b { print(\"{x}\") }
}

fn describe(f: Frame) ! console, alloc {
  match f {
    .data(d) {
      print(\"data {d.stream} {d.end_stream} {pad(d.padding)}\")
      bytes_of(d.data)
    }
    .headers(h) {
      print(\"headers {h.stream} {h.end_stream} {h.end_headers} {pad(h.padding)}\")
      match h.priority {
        .some(d) { print(\"dependency {d.exclusive} {d.stream} {d.weight}\") }
        .none { print(\"independent\") }
      }
      bytes_of(h.block)
    }
    .priority(s) { print(\"priority {s}\") }
    .reset(r) { print(\"reset {r.stream} {r.code}\") }
    .settings(pairs) {
      print(\"settings {pairs.size}\")
      loop (id, value) in pairs { print(\"{id} {value}\") }
    }
    .settings_ack { print(\"settings_ack\") }
    .push_promise(p) {
      print(\"promise {p.stream} {p.end_headers} {pad(p.padding)} {p.promised}\")
      bytes_of(p.block)
    }
    .ping(p) { print(\"ping {p.ack} {p.data}\") }
    .go_away(g) {
      print(\"go_away {g.last} {g.code}\")
      bytes_of(g.debug)
    }
    .window_update(w) { print(\"window {w.stream} {w.increment}\") }
    .continuation(c) {
      print(\"continuation {c.stream} {c.end_headers}\")
      bytes_of(c.block)
    }
    .unknown(k) { print(\"unknown {k}\") }
  }
}

fn verdict(b: Slice[Byte], largest: Nat) ! console, alloc {
  r = if largest == 16384 { frame(b) } else { frame(b, largest) }
  match r {
    .complete((f, used)) {
      print(\"complete {used}\")
      describe(f)
    }
    .incomplete { print(\"incomplete\") }
    .refused(why) { print(\"refused\") }
  }
}

fn same(a: List[Byte], b: Slice[Byte]) -> bool {
  if a.size != b.size { return false }
  loop i in 0..a.size {
    if i < b.size and a[i] != b[i] { return false }
  }
  true
}

fn written(f: Frame, expected: Slice[Byte]) ! console, alloc {
  l: List[Byte] = []
  print(\"written {same(write_frame(l, f), expected)}\")
}

fn early(b: Slice[Byte]) ! console, alloc {
  var count: Nat = 0
  loop k in 0..b.size {
    match frame(b[0..k]) {
      .incomplete { count = wrapping_add(count, 1) }
      .complete(r) { }
      .refused(why) { }
    }
  }
  print(\"incomplete prefixes {count}\")
}

";

fn a_frame_is_written_as_its_bytes_and_read_from_them_and_a_front_part_of_it_is_incomplete() {
    generate::check(
        59601,
        12,
        |g| {
            let frames: Vec<(F, u8, Vec<u8>)> = (0..g.below(10) + 4)
                .map(|_| {
                    let f = frame(g);
                    (f, g.below(256) as u8, some_bytes(g, 5))
                })
                .collect();
            frames
        },
        |frames| {
            let mut source = FRAMES.to_string();
            source += "fn main() ! console, alloc {\n";
            let mut expected = Vec::new();
            for (i, (f, noise, tail)) in frames.iter().enumerate() {
                let bytes = f.bytes();
                let mut noisy = bytes.clone();
                // What a reader is to ignore: flags that mean nothing in this kind of
                // frame, the reserved bit of each number, and what follows the frame.
                noisy[4] |= noise & !f.used_flags();
                noisy[5] |= 0x80;
                match f {
                    F::Promise { padding, .. } => noisy[9 + padding.map(|_| 1).unwrap_or(0)] |= 0x80,
                    F::GoAway { .. } | F::Window { .. } => noisy[9] |= 0x80,
                    F::Priority(_) => noisy[9..14].copy_from_slice(&[0x80 | noise, *noise, 3, 4, noise.wrapping_add(1)]),
                    _ => {}
                }
                noisy.extend(tail);
                let used = bytes.len();
                source += &format!("  written({}, {})\n", f.source(), list(&bytes));
                source += &format!("  verdict({}, 16384)\n  verdict({}, 16384)\n  early({})\n", list(&bytes), list(&noisy), list(&bytes));
                expected.push("written true".to_string());
                for _ in 0..2 {
                    expected.push(format!("complete {used}"));
                    expected.extend(f.describe());
                }
                expected.push(format!("incomplete prefixes {used}"));
                let _ = i;
            }
            source += "}\n";
            assert_eq!(lines(&source), expected, "{frames:?}");
        },
    );
}

/// The faults of a frame the page lists, each as the bytes of the frame with the
/// fault put in and the `largest` to read them with.
fn faults(f: &F) -> Vec<(&'static str, Vec<u8>, usize)> {
    let bytes = f.bytes();
    let payload = f.payload();
    let n = payload.len();
    let mut out: Vec<(&'static str, Vec<u8>, usize)> = Vec::new();
    // Longer than the reader reads.
    if n > 0 {
        out.push(("too long", bytes.clone(), n - 1));
    }
    // On the stream it should not be on.
    let needs_stream = matches!(
        f,
        F::Data { .. } | F::Headers { .. } | F::Priority(_) | F::Reset { .. } | F::Promise { .. } | F::Continuation { .. }
    );
    let needs_connection = matches!(f, F::Settings(_) | F::SettingsAck | F::Ping { .. } | F::GoAway { .. });
    let with_stream = |s: u32| {
        let mut b = bytes.clone();
        b[5..9].copy_from_slice(&be(u64::from(s), 4));
        b
    };
    if needs_stream {
        out.push(("on stream 0", with_stream(0), 16384));
    }
    if needs_connection {
        out.push(("on a stream", with_stream(1), 16384));
    }
    // Of a size its kind does not have.
    let resized = |length: usize| {
        let mut p = payload.clone();
        p.resize(length, 0);
        [be(length as u64, 3), bytes[3..9].to_vec(), p].concat()
    };
    match f {
        F::Priority(_) | F::Reset { .. } | F::Ping { .. } | F::Window { .. } => {
            out.push(("shorter", resized(n - 1), 16384));
            out.push(("longer", resized(n + 1), 16384));
            out.push(("empty", resized(0), 16384));
        }
        F::Settings(_) => {
            out.push(("a part of a setting", resized(n + 1), 16384));
            out.push(("a part of a setting", resized(n + 5), 16384));
        }
        F::SettingsAck => out.push(("acknowledging with settings", resized(6), 16384)),
        F::GoAway { .. } => out.push(("shorter than it must be", resized(7), 16384)),
        _ => {}
    }
    // Padded for more than the frame holds.
    let (padded, fixed) = match f {
        F::Data { padding: Some(_), .. } => (true, 0),
        F::Headers { padding: Some(_), priority, .. } => (true, if priority.is_some() { 5 } else { 0 }),
        F::Promise { padding: Some(_), .. } => (true, 4),
        _ => (false, 0),
    };
    if padded {
        let room = n - 1 - fixed;
        if room < 255 {
            let mut b = bytes.clone();
            b[9] = (room + 1) as u8;
            out.push(("padding longer than the frame", b, 16384));
        }
        out.push(("padded with no length of padding", [be(0, 3), bytes[3..9].to_vec()].concat(), 16384));
    }
    // Too short for the fields before its data.
    if let F::Headers { priority: Some(_), padding: None, .. } = f {
        out.push(("too short for its dependency", [be(3, 3), bytes[3..9].to_vec(), vec![0; 3]].concat(), 16384));
    }
    if let F::Promise { padding: None, .. } = f {
        out.push(("too short for its promise", [be(3, 3), bytes[3..9].to_vec(), vec![0; 3]].concat(), 16384));
    }
    out
}

fn a_frame_with_one_fault_the_page_lists_is_refused_and_without_it_is_read() {
    generate::check(
        59602,
        12,
        |g| (0..g.below(8) + 4).map(|_| frame(g)).collect::<Vec<F>>(),
        |frames| {
            let mut source = FRAMES.to_string();
            source += "fn main() ! console, alloc {\n";
            let mut expected = Vec::new();
            for f in frames {
                let bytes = f.bytes();
                source += &format!("  verdict({}, 16384)\n", list(&bytes));
                expected.push(format!("complete {}", bytes.len()));
                expected.extend(f.describe());
                for (_, bad, largest) in faults(f) {
                    source += &format!("  verdict({}, {largest})\n", list(&bad));
                    expected.push("refused".to_string());
                }
            }
            // A frame that says it is longer than the default is refused before the
            // rest of it is there.
            source += &format!("  verdict({}, 16384)\n", list(&[0, 0x40, 1, 0, 0, 0, 0, 0, 1]));
            expected.push("refused".to_string());
            source += "}\n";
            assert_eq!(lines(&source), expected, "{frames:?}");
        },
    );
}

fn bytes_of_any_kind_are_read_as_a_frame_or_refused_and_never_end_the_program() {
    generate::check(
        59603,
        10,
        |g| {
            (0..12)
                .map(|_| {
                    let mut b = some_bytes(g, 24);
                    // A header that says how long the rest is, most of the time.
                    if b.len() >= 9 && !g.one_in(4) {
                        let n = b.len() - 9;
                        b[0..3].copy_from_slice(&be(n as u64, 3));
                    }
                    b
                })
                .collect::<Vec<Vec<u8>>>()
        },
        |inputs| {
            let mut source = FRAMES.to_string();
            source += "fn main() ! console, alloc {\n";
            for b in inputs {
                source += &format!("  verdict({}, 16384)\n", list(b));
            }
            source += "}\n";
            let out = lines(&source);
            // Whatever is printed, a frame that is read took no more bytes than there were.
            for (line, b) in out.iter().filter(|l| l.starts_with("complete ")).zip(inputs.iter().filter(|b| b.len() >= 9)) {
                let used: usize = line["complete ".len()..].parse().unwrap();
                assert!(used >= 9 && used <= b.len() || used <= 9 + 16384, "{line} of {b:?}");
            }
        },
    );
}

/// The frame types, the settings, and the error codes that RFC 9113 gives numbers to are the numbers
/// the module reads and writes: a frame of each type is read as that frame, one of the next number is
/// not, and each setting and each error code is read as the number that holds it.
fn the_frame_types_settings_and_error_codes_of_rfc_9113_are_read_and_written_as_the_numbers_it_gives() {
    let types = rfc::frame_types();
    let named: Vec<&str> = types.iter().map(|(n, _)| n.as_str()).collect();
    assert_eq!(
        named,
        ["DATA", "HEADERS", "PRIORITY", "RST_STREAM", "SETTINGS", "PUSH_PROMISE", "PING", "GOAWAY", "WINDOW_UPDATE", "CONTINUATION"]
    );
    let sample = |name: &str| match name {
        "DATA" => F::Data { stream: 1, end_stream: true, padding: None, data: vec![1, 2] },
        "HEADERS" => F::Headers { stream: 1, end_stream: false, end_headers: true, padding: None, priority: None, block: vec![3] },
        "PRIORITY" => F::Priority(1),
        "RST_STREAM" => F::Reset { stream: 1, code: 8 },
        "SETTINGS" => F::Settings(vec![(1, 4096)]),
        "PUSH_PROMISE" => F::Promise { stream: 1, end_headers: true, padding: None, promised: 2, block: vec![4] },
        "PING" => F::Ping { ack: false, data: 7 },
        "GOAWAY" => F::GoAway { last: 3, code: 0, debug: vec![] },
        "WINDOW_UPDATE" => F::Window { stream: 1, increment: 5 },
        "CONTINUATION" => F::Continuation { stream: 1, end_headers: true, block: vec![5] },
        other => panic!("no frame is named {other}"),
    };
    let mut source = FRAMES.to_string();
    source += "fn main() ! console, alloc {\n";
    let mut expected = Vec::new();
    for (name, number) in &types {
        let f = sample(name);
        assert_eq!(f.kind(), *number, "{name}");
        let mut bytes = f.bytes();
        bytes[3] = *number;
        source += &format!("  verdict({}, 16384)\n", list(&bytes));
        expected.push(format!("complete {}", bytes.len()));
        expected.extend(f.describe());
    }
    // The type after the last that the standard names is passed over.
    let after = types.last().unwrap().1 + 1;
    let mut bytes = sample("CONTINUATION").bytes();
    bytes[3] = after;
    source += &format!("  verdict({}, 16384)\n", list(&bytes));
    expected.push(format!("complete {}", bytes.len()));
    expected.push(format!("unknown {after}"));
    // Each setting, and each error code in a reset and in a go away.
    let settings = rfc::settings();
    assert_eq!(settings.len(), 6);
    let ids: Vec<(u32, u32)> = settings.iter().map(|(_, id)| (*id, 7)).collect();
    let f = F::Settings(ids);
    source += &format!("  verdict({}, 16384)\n", list(&f.bytes()));
    expected.push(format!("complete {}", f.bytes().len()));
    expected.extend(f.describe());
    let codes = rfc::error_codes();
    assert_eq!(codes.len(), 14);
    for (_, code) in &codes {
        for f in [F::Reset { stream: 1, code: *code }, F::GoAway { last: 1, code: *code, debug: vec![] }] {
            source += &format!("  verdict({}, 16384)\n", list(&f.bytes()));
            expected.push(format!("complete {}", f.bytes().len()));
            expected.extend(f.describe());
        }
    }
    source += "}\n";
    assert_eq!(lines(&source), expected);
}

// The headers.

/// The start of every program that reads headers: what it prints of a result,
/// and `probe`, which reads one reference to each place of the dynamic table
/// from the first to one past the last, so that a table is seen entry by entry
/// through the module alone.
const HEADERS: &str = "use std.console (console, print)
use std.http2 (Decoder, Encoder, decode, decoder, encode, encoder)
use std.int (wrapping_add, wrapping_narrow)
use std.list (freeze, push)

fn show(r: Result[List[(str, str)], str]) ! console, alloc {
  match r {
    .ok(headers) {
      loop (n, v) in headers { print(\"{n}: {v}\") }
      print(\"end\")
    }
    .err(why) { print(\"error\") }
  }
}

fn reference(i: Nat) -> Slice[Byte] ! alloc {
  var l: List[Byte] = []
  l = push(l, wrapping_narrow(wrapping_add(128, i)))
  freeze(l)
}

fn probe(d: take Decoder, places: Nat) -> Decoder ! console, alloc {
  var t = d
  loop i in 0..wrapping_add(places, 1) {
    (next, r) = decode(t, reference(wrapping_add(62, i)), 65536)
    t = next
    show(r)
  }
  t
}

";

/// What `show` prints for headers.
fn shown<N: AsRef<str>, V: AsRef<str>>(headers: &[(N, V)]) -> Vec<String> {
    let mut out: Vec<String> = headers.iter().map(|(n, v)| format!("{}: {}", n.as_ref(), v.as_ref())).collect();
    out.push("end".to_string());
    out
}

fn the_examples_of_rfc_7541_appendix_c_decode_to_the_headers_and_the_table_it_gives() {
    for series in rfc::appendix_c() {
        let mut source = HEADERS.to_string();
        source += "fn main() ! console, alloc {\n";
        let table = series.table;
        source += &if table == 4096 { "  d0 = decoder()\n".to_string() } else { format!("  d0 = decoder({table})\n") };
        let mut expected = Vec::new();
        for (n, example) in series.examples.iter().enumerate() {
            source += &format!(
                "  (a{n}, r{n}) = decode(d{n}, {})\n  show(r{n})\n  d{} = probe(a{n}, {})\n",
                list(&example.block),
                n + 1,
                example.table.len()
            );
            expected.extend(shown(&example.headers));
            for (name, value, size) in &example.table {
                assert_eq!(name.len() + value.len() + 32, *size, "the appendix");
                expected.extend(shown(&[(name, value)]));
            }
            expected.push("error".to_string());
        }
        source += "}\n";
        assert_eq!(lines(&source), expected, "{}", series.name);
    }
}

/// The headers of the examples of appendix C are encoded to blocks that read back to them,
/// and, where the appendix writes each text in the short code, to its bytes: the page lets
/// the encoder write a text in the short code when that is no longer than the text, which is
/// what the appendix does in sections 4 and 6, and has it write `set-cookie` never indexed, which
/// the appendix does not in the last block of section 5 and of section 6. An encoder made for
/// a table of other than 4096 bytes writes the change of its size first.
fn the_headers_of_appendix_c_are_encoded_to_blocks_that_read_back_and_to_the_bytes_of_the_appendix_where_the_page_allows() {
    let all = rfc::appendix_c();
    assert_eq!(all.len(), 8);
    for (at, series) in all.iter().enumerate().skip(4) {
        let (table, short) = (series.table, at == 5 || at == 7);
        let mut source = HEADERS.to_string();
        source += "fn empty() -> List[Byte] { [] }\n\nfn dump(w: List[Byte]) ! console, alloc {\n  loop b in w { print(\"{b}\") }\n  print(\"end\")\n}\n\n";
        for (n, example) in series.examples.iter().enumerate() {
            source += &function(&format!("headers{n}"), &example.headers);
        }
        let make = |name: &str| if table == 4096 { format!("{name}()") } else { format!("{name}({table})") };
        source += &format!("\nfn main() ! console, alloc {{\n  e0 = {}\n  d0 = {}\n", make("encoder"), make("decoder"));
        let mut expected = Vec::new();
        for (n, example) in series.examples.iter().enumerate() {
            let secret = example.headers.iter().any(|(name, _)| name == "set-cookie");
            let update = if n == 0 && table != 4096 { number(0x20, 5, table) } else { Vec::new() };
            source += &format!(
                "  (e{m}, w{n}) = encode(e{n}, empty(), headers{n}())\n  dump(w{n})\n  (a{n}, r{n}) = decode(d{n}, freeze(w{n}), 65536)\n  show(r{n})\n",
                m = n + 1
            );
            if secret {
                source += &format!("  d{} = a{n}\n", n + 1);
            } else {
                source += &format!("  d{} = probe(a{n}, {})\n", n + 1, example.table.len());
            }
            // The bytes are the appendix's where it writes each text in the short code, and not
            // where the block has a header the encoder never indexes.
            expected.push((short && !secret, [update, example.block.clone()].concat()));
        }
        source += "}\n";
        let out = lines(&source);
        let mut at = 0;
        for (n, (same, bytes)) in expected.iter().enumerate() {
            let end = out[at..].iter().position(|l| l == "end").unwrap() + at;
            let written: Vec<u8> = out[at..end].iter().map(|l| l.parse().unwrap()).collect();
            if *same {
                assert_eq!(&written, bytes, "{} block {n}", series.name);
            }
            at = end + 1;
            let example = &series.examples[n];
            let mut want = shown(&example.headers);
            if !example.headers.iter().any(|(name, _)| name == "set-cookie") {
                for (name, value, _) in &example.table {
                    want.extend(shown(&[(name, value)]));
                }
                want.push("error".to_string());
            }
            assert_eq!(out[at..at + want.len()], want[..], "{} block {n}", series.name);
            at += want.len();
        }
        assert_eq!(at, out.len());
    }
}

fn the_static_table_is_the_one_of_rfc_7541_appendix_a() {
    let mut source = HEADERS.to_string();
    source += "fn main() ! console, alloc {\n  d0 = decoder(4096)\n";
    let mut expected = Vec::new();
    for (i, (name, value)) in rfc::static_table().iter().enumerate() {
        source += &format!("  (d{}, r{i}) = decode(d{i}, {}, 65536)\n  show(r{i})\n", i + 1, list(&[0x80 | (i as u8 + 1)]));
        expected.extend(shown(&[(name, value)]));
    }
    source += "}\n";
    assert_eq!(lines(&source), expected);
}

/// The numbers of appendix C.1 are read in the 5 bits of the change of a table's size: a decoder
/// that is agreed a table of that size takes it, and one of a byte less does not.
fn the_numbers_of_rfc_7541_appendix_c_1_are_read_from_their_bytes() {
    let numbers = rfc::integers();
    assert_eq!(numbers, vec![10, 1337]);
    let mut source = HEADERS.to_string();
    source += "fn main() ! console, alloc {\n";
    let mut expected = Vec::new();
    for (i, n) in numbers.iter().enumerate() {
        let block = number(0x20, 5, *n);
        for (j, table) in [*n, n - 1].into_iter().enumerate() {
            source += &format!("  (d{i}x{j}, r{i}x{j}) = decode(decoder({table}), {}, 65536)\n  show(r{i}x{j})\n", list(&block));
        }
        expected.extend(["end", "error"].map(String::from));
    }
    source += "}\n";
    assert_eq!(lines(&source), expected);
}

/// The text of every symbol that is the start of no bad UTF-8: each byte but the 11 that no text
/// holds, with a character for each lead byte and each byte after it.
fn every_character() -> Vec<u8> {
    let mut text = String::new();
    // Every byte of one, and a lead byte and a byte after it of each of two.
    text.extend((0u32..128).filter_map(char::from_u32));
    for i in 0..64u32 {
        let (lead, after) = (0xc2 + i % 30, 0x80 + i);
        text.push(char::from_u32(((lead & 0x1f) << 6) | (after & 0x3f)).unwrap());
    }
    // A lead byte of each of the three bytes, from e0 to ef.
    text.extend([0x800].into_iter().chain((1u32..16).map(|i| i * 0x1000)).map(|c| char::from_u32(c).unwrap()));
    text.extend([0x10000, 0x40000, 0x80000, 0xc0000, 0x100000].map(|c| char::from_u32(c).unwrap()));
    text.into_bytes()
}

/// The code of the standard is what the module reads and writes: a text of every byte a text
/// holds is read from the code of the appendix, and is written as the code of the appendix, and a
/// byte no text holds is read as a symbol, which is a text that is not UTF-8.
fn the_short_code_of_rfc_7541_appendix_b_is_read_and_written_for_every_byte() {
    // Cheap characters first, so that the short code is no longer than the text.
    let text = [vec![b'0'; 2000], every_character()].concat();
    assert!(huffman(&text).len() <= text.len());
    let seen: std::collections::BTreeSet<u8> = text.iter().copied().collect();
    let unreachable: Vec<u8> = (0..=255u8).filter(|b| !seen.contains(b)).collect();
    // Only the bytes that no text of UTF-8 holds are not in the text.
    assert_eq!(unreachable, vec![0xc0, 0xc1, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff]);
    let mut source = HEADERS.replace("use std.list (freeze, push)\n", "use std.list (freeze, push)\nuse std.utf8 (utf8)\n");
    source += "fn empty() -> List[Byte] { [] }\n\nfn text(b: take List[Byte]) -> str {\n  match utf8(freeze(b)) {\n    .ok(t) { t }\n    .err(e) { \"\" }\n  }\n}\n\nfn dump(w: List[Byte]) ! console, alloc {\n  loop b in w { print(\"{b}\") }\n  print(\"end\")\n}\n\n";
    source += &format!("fn value() -> str ! alloc {{ text({}) }}\n\n", list(&text));
    source += "fn main() ! console, alloc {\n  (e, w) = encode(encoder(), empty(), [(\"x\", value())])\n  dump(w)\n";
    let block = [vec![0x00], string(b"x", false), string(&text, true)].concat();
    source += &format!("  (d, r) = decode(decoder(), {}, 65536)\n  show(r)\n", list(&block));
    // A symbol that no text holds, between two texts of one byte.
    let mut expected = Vec::new();
    for (i, b) in unreachable.iter().enumerate() {
        let block = [vec![0x00], string(b"x", false), string(&[b'a', *b, b'a'], true)].concat();
        source += &format!("  (d{i}, r{i}) = decode(decoder(), {}, 65536)\n  show(r{i})\n", list(&block));
        expected.push("error".to_string());
    }
    source += "}\n";
    let out = lines(&source);
    // The module writes the value as the appendix does, with the name as it is written when
    // the short code is no longer, and adds the entry to its table.
    let written = [vec![0x40], string(b"x", true), string(&text, true)].concat();
    let end = out.iter().position(|l| l == "end").unwrap();
    let got: Vec<u8> = out[..end].iter().map(|l| l.parse().unwrap()).collect();
    assert_eq!(got, written);
    // The text has line breaks in it, so what is printed is compared as it is.
    let value = String::from_utf8(text).unwrap();
    let printed = out[end + 1..].join("\n");
    let wanted = format!("x: {value}\nend\n{}", expected.join("\n"));
    assert_eq!(printed, wanted);
}

// A writer of HPACK, to build the blocks that the module is not to have written.

/// A number with a prefix of `bits` bits, after the `pattern` in the bits before it.
fn number(pattern: u8, bits: u32, value: usize) -> Vec<u8> {
    let mask = (1usize << bits) - 1;
    if value < mask {
        return vec![pattern | value as u8];
    }
    let mut out = vec![pattern | mask as u8];
    let mut rest = value - mask;
    while rest >= 128 {
        out.push((rest % 128) as u8 | 128);
        rest /= 128;
    }
    out.push(rest as u8);
    out
}

/// The text in the short code, with the padding of the end of a text.
fn huffman(text: &[u8]) -> Vec<u8> {
    let mut out = Vec::new();
    let (mut acc, mut n) = (0u64, 0u32);
    for b in text {
        let (code, len) = rfc::huffman()[*b as usize];
        acc = (acc << len) | u64::from(code);
        n += len;
        while n >= 8 {
            n -= 8;
            out.push((acc >> n) as u8);
        }
        acc &= (1 << n) - 1;
    }
    if n > 0 {
        let fill = 8 - n;
        out.push(((acc << fill) | ((1 << fill) - 1)) as u8);
    }
    out
}

/// A string of a block, as it is or in the short code.
fn string(text: &[u8], short: bool) -> Vec<u8> {
    if short {
        let h = huffman(text);
        [number(0x80, 7, h.len()), h].concat()
    } else {
        [number(0, 7, text.len()), text.to_vec()].concat()
    }
}

/// A literal header field that is not added to the table, with its name as text.
fn literal(name: &[u8], value: &[u8], short: bool) -> Vec<u8> {
    [vec![0], string(name, short), string(value, short)].concat()
}

/// One representation of a block, as the walker finds it.
#[derive(Debug)]
struct Rep {
    /// The bits that start it: 0x80 an index, 0x40 added to the table, 0x20 a
    /// change of the table's size, 0x10 never added, and 0 not added.
    kind: u8,
    start: usize,
    end: usize,
}

/// Reads a number, with where it ends.
fn read_number(b: &[u8], at: usize, bits: u32) -> (usize, usize) {
    let mask = (1usize << bits) - 1;
    let first = b[at] as usize & mask;
    if first < mask {
        return (first, at + 1);
    }
    let (mut value, mut shift, mut i) = (mask, 0, at + 1);
    loop {
        value += (b[i] as usize & 127) << shift;
        i += 1;
        shift += 7;
        if b[i - 1] < 128 {
            return (value, i);
        }
    }
}

/// Finds the representations of a block, from the standard.
fn walk(b: &[u8]) -> Vec<Rep> {
    let mut reps = Vec::new();
    let mut at = 0;
    while at < b.len() {
        let c = b[at];
        let (kind, bits) = if c & 0x80 != 0 {
            (0x80, 7)
        } else if c & 0x40 != 0 {
            (0x40, 6)
        } else if c & 0x20 != 0 {
            (0x20, 5)
        } else if c & 0x10 != 0 {
            (0x10, 4)
        } else {
            (0, 4)
        };
        let start = at;
        let (index, next) = read_number(b, at, bits);
        at = next;
        if kind & 0xa0 == 0 {
            for _ in 0..(if index == 0 { 2 } else { 1 }) {
                let (len, next) = read_number(b, at, 7);
                at = next + len;
            }
        }
        reps.push(Rep { kind, start, end: at });
    }
    reps
}

// The headers of a run.

const NAMES: &[&str] = &[
    ":method", ":path", ":status", "accept", "accept-encoding", "content-type", "cache-control", "x-request-id",
    "authorization", "cookie", "set-cookie", "x-custom", "vary", "a", "",
];

const VALUES: &[&str] = &[
    "", "GET", "/", "text/html", "gzip, deflate", "no-cache", "max-age=0", "x", "www.example.com", "é", "中文", "😀",
    "a value with spaces and symbols: ~!@#$%^&*()_+-=[]{}|;:'<>,.?/", "{braces}", "quote\"s and \\ slashes",
];

fn text(g: &mut Gen) -> String {
    const CHARS: &[char] = &['a', 'b', 'Z', '0', '9', ' ', '-', '_', '/', ':', 'é', 'ß', 'ж', '€', '中', '\u{1F600}', '~', '='];
    (0..g.below(40)).map(|_| *g.pick(CHARS)).collect()
}

fn header(g: &mut Gen) -> (String, String) {
    let name = if g.one_in(4) { text(g) } else { g.pick(NAMES).to_string() };
    let value = if g.one_in(3) { text(g) } else { g.pick(VALUES).to_string() };
    (name, value)
}

fn headers(g: &mut Gen) -> Vec<(String, String)> {
    (0..g.below(7)).map(|_| header(g)).collect()
}

/// The headers as a function of the program.
fn function(name: &str, headers: &[(String, String)]) -> String {
    let items: Vec<String> = headers.iter().map(|(n, v)| format!("({}, {})", quoted(n), quoted(v))).collect();
    format!("fn {name}() -> List[(str, str)] ! alloc {{ [{}] }}\n", items.join(", "))
}

fn nested(h: &[(String, String)]) -> Vec<(&str, &str)> {
    h.iter().map(|(n, v)| (n.as_str(), v.as_str())).collect()
}

fn decoding_what_was_encoded_gives_the_headers_over_blocks_with_one_encoder_and_one_decoder() {
    generate::check(
        59604,
        8,
        |g| {
            let table = *g.pick(&[4096, 0, 60, 256, 100, 8192]);
            let blocks: Vec<_> = (0..g.below(5) + 2).map(|_| headers(g)).collect();
            (table, blocks)
        },
        |(table, blocks)| {
            let mut source = HEADERS.to_string();
            source += "fn empty() -> List[Byte] { [] }\n\n";
            for (i, h) in blocks.iter().enumerate() {
                source += &function(&format!("headers{i}"), h);
            }
            source += &format!("\nfn main() ! console, alloc {{\n  e0 = encoder({table})\n  d0 = decoder({table})\n");
            let mut expected = Vec::new();
            for (i, h) in blocks.iter().enumerate() {
                source += &format!(
                    "  (e{n}, w{i}) = encode(e{i}, empty(), headers{i}())\n  (d{n}, r{i}) = decode(d{i}, freeze(w{i}), 65536)\n  show(r{i})\n",
                    n = i + 1
                );
                expected.extend(shown(&nested(h)));
            }
            source += "}\n";
            assert_eq!(lines(&source), expected, "{table} {blocks:?}");
        },
    );
}

fn a_block_is_an_error_where_the_page_says_and_a_block_without_that_fault_is_read() {
    // Each case is blocks read by one decoder with a table of the size given, and
    // what each block gives: the headers, or an error.
    type Case = (&'static str, usize, Vec<(Vec<u8>, Option<Vec<(&'static str, &'static str)>>)>);
    let first = unhex("828684410f7777772e6578616d706c652e636f6d");
    let www = ("www.example.com", "");
    let _ = www;
    let authority = vec![(":method", "GET"), (":scheme", "http"), (":path", "/"), (":authority", "www.example.com")];
    let cases: Vec<Case> = vec![
        ("an index of 0", 4096, vec![(vec![0x80], None)]),
        ("the last place of the static table", 4096, vec![(vec![0xbd], Some(vec![("www-authenticate", "")]))]),
        ("a place that names no header, with the table empty", 4096, vec![(vec![0xbe], None)]),
        (
            "a place beyond the dynamic table",
            4096,
            vec![(first.clone(), Some(authority.clone())), (vec![0xbf], None)],
        ),
        (
            "the place of the one entry",
            4096,
            vec![(first.clone(), Some(authority.clone())), (vec![0xbe], Some(vec![(":authority", "www.example.com")]))],
        ),
        ("a number that ends no more", 4096, vec![(vec![0xff, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x00], None)]),
        ("a block that ends in a number", 4096, vec![(vec![0xff, 0x80], None)]),
        ("a number that is too great", 4096, vec![(vec![0xff, 0xff, 0xff, 0xff, 0xff, 0x7f], None)]),
        ("a text that is longer than the block", 4096, vec![(vec![0x00, 0x05, b'a'], None)]),
        ("a text of a great length", 4096, vec![(vec![0x00, 0x01, b'a', 0x7f, 0xff, 0xff, 0x7f], None)]),
        (
            "the same entry added twice",
            4096,
            vec![(
                vec![0x40, 0x01, b'a', 0x01, b'b', 0x40, 0x01, b'a', 0x01, b'b', 0xbe, 0xbf],
                Some(vec![("a", "b"), ("a", "b"), ("a", "b"), ("a", "b")]),
            )],
        ),
        ("a block that ends before the value", 4096, vec![(vec![0x00, 0x01, b'a'], None)]),
        ("a value that is not UTF-8", 4096, vec![(vec![0x00, 0x01, b'a', 0x01, 0xff], None)]),
        ("a name that is not UTF-8", 4096, vec![(vec![0x00, 0x01, 0xc3, 0x01, b'a'], None)]),
        ("a change of the table's size at the start", 4096, vec![(vec![0x20], Some(vec![]))]),
        ("changes of the table's size at the start", 4096, vec![(vec![0x20, 0x21, 0x82], Some(vec![(":method", "GET")]))]),
        ("a change of the table's size after a header", 4096, vec![(vec![0x82, 0x20], None)]),
        ("a change of the table's size to what was agreed", 100, vec![(vec![0x3f, 0x45], Some(vec![]))]),
        ("a change of the table's size to more than was agreed", 100, vec![(vec![0x3f, 0x46], None)]),
        (
            "a change of the table's size to nothing, which takes the entries out",
            4096,
            vec![(first.clone(), Some(authority.clone())), (vec![0x20, 0xbe], None)],
        ),
        (
            "a change of the table's size that keeps the entry",
            4096,
            vec![
                (first.clone(), Some(authority.clone())),
                (vec![0x3f, 0xe1, 0x1f, 0xbe], Some(vec![(":authority", "www.example.com")])),
            ],
        ),
        (
            "a change of the table's size that is too little for the entry",
            4096,
            vec![(first.clone(), Some(authority.clone())), (vec![0x3f, 0x19, 0xbe], None)],
        ),
    ];
    let mut source = HEADERS.to_string();
    source += "fn main() ! console, alloc {\n";
    let mut expected = Vec::new();
    for (c, (_, table, blocks)) in cases.iter().enumerate() {
        source += &format!("  d{c}x0 = decoder({table})\n");
        for (i, (block, gives)) in blocks.iter().enumerate() {
            source += &format!(
                "  (d{c}x{n}, r{c}x{i}) = decode(d{c}x{i}, {}, 65536)\n  show(r{c}x{i})\n",
                list(block),
                n = i + 1
            );
            match gives {
                Some(h) => expected.extend(shown(h)),
                None => expected.push("error".to_string()),
            }
        }
    }
    source += "}\n";
    let got = lines(&source);
    // Name the case that differs.
    let mut at = 0;
    for (name, _, blocks) in &cases {
        let want: Vec<String> = blocks
            .iter()
            .flat_map(|(_, gives)| match gives {
                Some(h) => shown(h),
                None => vec!["error".to_string()],
            })
            .collect();
        assert_eq!(got[at..at + want.len()], want[..], "{name}");
        at += want.len();
    }
    assert_eq!(got, expected);
}

fn a_text_in_the_short_code_is_read_and_one_with_wrong_padding_or_its_end_is_an_error() {
    generate::check(
        59605,
        16,
        |g| (text(g), g.below(4)),
        |(value, fault)| {
            let coded = huffman(value.as_bytes());
            // The padding is the bits after the last code, which are 1: how many there
            // are is what the codes of the text leave of its last byte.
            let bits: u32 = value.bytes().map(|b| rfc::huffman()[b as usize].1).sum();
            let padding = (8 - bits % 8) % 8;
            let (bad, fault_is_there): (Vec<u8>, bool) = match fault {
                0 => (coded.clone(), false),
                // Padding of 8 bits or more.
                1 => ([coded.clone(), vec![0xff]].concat(), true),
                // Padding that has a 0 bit, which is not the start of the end of a text.
                2 if padding > 0 => {
                    let mut c = coded.clone();
                    *c.last_mut().unwrap() &= !1;
                    (c, true)
                }
                // The end of a text inside the text.
                _ => ([coded.clone(), vec![0xff, 0xff, 0xff, 0xff]].concat(), true),
            };
            let block = [vec![0x00], string(b"a", false), number(0x80, 7, bad.len()), bad].concat();
            let source = HEADERS.to_string()
                + &format!("fn main() ! console, alloc {{\n  (d, r) = decode(decoder(4096), {}, 65536)\n  show(r)\n}}\n", list(&block));
            let expected = if fault_is_there { vec!["error".to_string()] } else { shown(&[("a", value)]) };
            assert_eq!(lines(&source), expected, "{value:?} {fault}");
        },
    );
}

fn a_block_of_more_than_largest_bytes_is_an_error_and_of_largest_or_fewer_is_read() {
    generate::check(
        59606,
        10,
        |g| (g.below(2) == 1, (0..g.below(5) + 1).map(|_| header(g)).collect::<Vec<_>>()),
        |(short, h)| {
            // The headers as a block that is not made by the module, so the test holds
            // for the short code and for text as it is.
            let mut block = Vec::new();
            for (n, v) in h {
                block.extend(literal(n.as_bytes(), v.as_bytes(), *short));
            }
            let total: usize = h.iter().map(|(n, v)| n.len() + v.len()).sum();
            let mut source = HEADERS.to_string();
            source += "fn main() ! console, alloc {\n";
            source += &format!("  (a, r) = decode(decoder(4096), {}, {total})\n  show(r)\n", list(&block));
            if total > 0 {
                source += &format!("  (b, s) = decode(decoder(4096), {}, {})\n  show(s)\n", list(&block), total - 1);
            }
            source += "}\n";
            let mut expected = shown(&nested(h));
            if total > 0 {
                expected.push("error".to_string());
            }
            assert_eq!(lines(&source), expected, "{h:?}");
        },
    );
}

fn a_block_that_comes_to_more_than_largest_bytes_is_an_error_before_that_much_memory_is_asked_for() {
    // A text in the short code of `m` times 8 characters `0`, which are 5 bits of 0 each,
    // so that 5 bytes of 0 are 8 characters and there is no padding.
    for (m, largest) in [(200usize, 64usize), (1000, 100), (600, 5000)] {
        let value = vec![0u8; 5 * m];
        let characters = 8 * m;
        let block = [vec![0x00], string(b"a", false), number(0x80, 7, value.len()), value].concat();
        let source = HEADERS.to_string()
            + &format!("fn main() ! console, alloc {{\n  (d, r) = decode(decoder(4096), {}, {largest})\n  show(r)\n}}\n", list(&block));
        let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{source}"));
        let (outcome, taken) = scalar::run_taking(&program, usize::MAX);
        assert_eq!(outcome.status, 0);
        let text = String::from_utf8(outcome.stdout).unwrap();
        let comes_to = 1 + characters;
        // A request with the top bit set asks for what is left when that is its size.
        let asked: usize = taken.iter().map(|n| (*n & !(1 << 63)).next_multiple_of(8)).sum();
        if comes_to > largest {
            assert_eq!(text, "error\n", "{m} {largest}");
            // What was asked for is what the text came to before it was too much, and not
            // what the block would have come to.
            assert!(asked <= 16 * largest + 1024, "{asked} bytes for {largest}, of {taken:?}");
            assert!(asked < characters, "{asked} bytes for a text of {characters}");
        } else {
            let want = format!("a: {}\nend\n", "0".repeat(characters));
            assert_eq!(text, want);
            assert!(asked >= characters, "{asked} bytes for a text of {characters}");
        }
    }
}

fn a_header_named_authorization_cookie_or_set_cookie_is_written_never_indexed_and_learned_by_no_one() {
    const SECRETS: &[&str] = &["authorization", "cookie", "set-cookie"];
    generate::check(
        59607,
        15,
        |g| {
            let mut list: Vec<(String, String)> = (0..g.below(4) + 1).map(|_| header(g)).collect();
            for _ in 0..g.below(3) + 1 {
                list.insert(g.below(list.len() + 1), (g.pick(SECRETS).to_string(), text(g)));
            }
            (*g.pick(&[4096usize, 256]), list)
        },
        |(table, h)| {
            let mut source = HEADERS.to_string();
            source += "fn empty() -> List[Byte] { [] }\n\n";
            source += &function("headers", h);
            source += &format!(
                "\nfn dump(w: List[Byte]) ! console, alloc {{\n  loop b in w {{ print(\"{{b}}\") }}\n  print(\"end\")\n}}\n\nfn main() ! console, alloc {{\n  e0 = encoder({table})\n  (e1, w1) = encode(e0, empty(), headers())\n  (e2, w2) = encode(e1, empty(), headers())\n  dump(w1)\n  dump(w2)\n  d0 = decoder({table})\n  (d1, r1) = decode(d0, freeze(w1), 65536)\n  (d2, r2) = decode(d1, freeze(w2), 65536)\n  show(r2)\n  d3 = probe(d2, 40)\n}}\n"
            );
            let out = lines(&source);
            let mut blocks: Vec<Vec<u8>> = Vec::new();
            let mut at = 0;
            for _ in 0..2 {
                let end = out[at..].iter().position(|l| l == "end").unwrap() + at;
                blocks.push(out[at..end].iter().map(|l| l.parse().unwrap()).collect());
                at = end + 1;
            }
            // The representations of the secret headers are never indexed, and are the same
            // bytes the second time, so that nothing the first block holds is learned.
            let mut secrets = [Vec::new(), Vec::new()];
            for (i, block) in blocks.iter().enumerate() {
                let reps: Vec<Rep> = walk(block).into_iter().filter(|r| r.kind != 0x20).collect();
                assert_eq!(reps.len(), h.len(), "a representation for each header in {block:?}");
                for ((name, _), rep) in h.iter().zip(&reps) {
                    if SECRETS.contains(&name.as_str()) {
                        assert_eq!(rep.kind, 0x10, "{name} is not written never indexed in {block:?}");
                        secrets[i].push(block[rep.start..rep.end].to_vec());
                    }
                }
            }
            assert_eq!(secrets[0], secrets[1]);
            // What the decoder holds, which is what the first block taught it and what the
            // second did, has none of them.
            let held: Vec<String> = out[at..].to_vec();
            for name in SECRETS {
                assert!(!held.iter().any(|l| l.starts_with(&format!("{name}: "))) || {
                    // The headers themselves are printed first, up to the first `end`.
                    let first_end = held.iter().position(|l| l == "end").unwrap();
                    !held[first_end..].iter().any(|l| l.starts_with(&format!("{name}: ")))
                }, "{name} is in the table of the decoder: {held:?}");
            }
        },
    );
}

fn blocks_of_any_bytes_are_read_or_an_error_and_never_end_the_program() {
    generate::check(
        59608,
        10,
        |g| {
            (0..10)
                .map(|_| {
                    let mut b = some_bytes(g, 24);
                    // Mostly the starts of representations, so that a block gets as far as its texts.
                    for byte in b.iter_mut() {
                        if g.one_in(3) {
                            *byte = *g.pick(&[0x00, 0x40, 0x10, 0x80, 0x82, 0xbe, 0x20, 0x01, 0x7f, 0xff, 0x3f]);
                        }
                    }
                    b
                })
                .collect::<Vec<_>>()
        },
        |blocks| {
            let mut source = HEADERS.to_string();
            source += "fn main() ! console, alloc {\n";
            for (i, b) in blocks.iter().enumerate() {
                source += &format!("  (d{i}, r{i}) = decode(decoder(256), {}, 64)\n  show(r{i})\n", list(b));
            }
            source += "}\n";
            let out = lines(&source);
            // Each block gives headers and `end`, or `error`, and no header is longer than a block allows.
            assert_eq!(out.iter().filter(|l| *l == "end" || *l == "error").count(), blocks.len());
        },
    );
}
