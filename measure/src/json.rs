//! `std.json` writing a list of 100,000 small structs, and reading the text it
//! wrote, each beside the same in Rust written by hand, with no library: the
//! time and the instructions for each byte of the text, and the time to build
//! each program and the size of its executable.

use std::path::Path;
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

use crate::build::{instructions, per_byte, size};
use crate::profile;
use crate::{RUNS, least, smith_time};

/// How many structs are written.
const ITEMS: usize = 100_000;

/// The bytes of the text of the list: each struct is
/// `{"id":N,"count":-5,"on":true}`, and the structs are in an array.
fn text_size() -> usize {
    let mut bytes = 2 + (ITEMS - 1);
    for i in 0..ITEMS {
        let digits = i.to_string().len();
        let on = if i % 2 == 0 { 4 } else { 5 };
        bytes += r#"{"id":,"count":-5,"on":}"#.len() + digits + on;
    }
    bytes
}

/// The Smith program, which builds the list `reps` times and does `body`
/// with it each time, and prints the last number `body` gives.
fn smith_program(reps: usize, body: &str) -> String {
    format!(
        "use std.bits (bit_and, i32, u32)
use std.console (console, print)
use std.json (to_json)
use std.list (push)

struct Item {{
  id: u32
  count: i32
  on: bool
}}

fn main() ! console, alloc {{
  var last: Nat = 0
  loop k in 0..{reps} {{
    var xs: List[Item] = []
    loop i in 0..{ITEMS} {{ xs = push(xs, Item(id: i, count: -5, on: bit_and(i, 1) == 0)) }}
    {body}
  }}
  print(\"{{last}}\")
}}
"
    )
}

/// The same in Rust, with each number written by hand into a vector of
/// bytes.
const RUST: &str = r#"use std::hint::black_box;

struct Item {
    id: u32,
    count: i32,
    on: bool,
}

fn number(out: &mut Vec<u8>, n: i64) {
    if n < 0 {
        out.push(b'-');
    }
    let mut n = n.unsigned_abs();
    let mut digits = [0u8; 20];
    let mut at = digits.len();
    loop {
        at -= 1;
        digits[at] = b'0' + (n % 10) as u8;
        n /= 10;
        if n == 0 {
            break;
        }
    }
    out.extend_from_slice(&digits[at..]);
}

fn write(items: &[Item]) -> Vec<u8> {
    let mut out = Vec::new();
    out.push(b'[');
    for (i, item) in items.iter().enumerate() {
        if i > 0 {
            out.push(b',');
        }
        out.extend_from_slice(b"{\"id\":");
        number(&mut out, item.id as i64);
        out.extend_from_slice(b",\"count\":");
        number(&mut out, item.count as i64);
        out.extend_from_slice(b",\"on\":");
        out.extend_from_slice(if item.on { b"true" } else { b"false" });
        out.push(b'}');
    }
    out.push(b']');
    out
}

fn main() {
    let mut items = Vec::new();
    for i in 0..ITEMS as u32 {
        items.push(Item { id: i, count: -5, on: i & 1 == 0 });
    }
    let mut last = 0;
    for _ in 0..REPS {
        last = write(black_box(&items)).len();
    }
    println!("{last}");
}
"#;

pub fn run(smith: &Path, work: &Path) {
    write(smith, work);
    read(smith, work);
}

fn write(smith: &Path, work: &Path) {
    let bytes = text_size();
    let expected = bytes.to_string();

    // The Smith program: its build, and its run net of building the list.
    let (time, reps) = smith_time(smith, work, "json", &expected, |reps| {
        (
            smith_program(reps, "last = to_json(xs).size"),
            smith_program(reps, "last = xs.size"),
        )
    });
    // The base program prints the number of structs, not of bytes, so its
    // output is not the text's size and is not checked as it.
    let built = build_time(smith, work, "json");
    let smith_exe = work.join("json-whole");
    println!(
        "json smith build_ms={:.2} bytes={} ns_per_byte={}",
        built.as_secs_f64() * 1000.0,
        size(&smith_exe),
        per_byte(time, bytes),
    );

    // The Rust program, as is and with `-O`, which writes the list once.
    let main = work.join("json_rust.rs");
    std::fs::write(
        &main,
        format!("const ITEMS: usize = {ITEMS};\nconst REPS: usize = 1;\n{RUST}"),
    )
    .unwrap();
    let mut rust_exes = Vec::new();
    for (who, flags) in [("rust", &[][..]), ("rust_O", &["-O"][..])] {
        let exe = work.join(format!("json_{who}"));
        let mut best: Option<Duration> = None;
        for _ in 0..RUNS {
            let start = Instant::now();
            let status = Command::new("rustc")
                .args(["--edition", "2024", "-o"])
                .arg(&exe)
                .args(flags)
                .arg(&main)
                .stderr(Stdio::null())
                .status()
                .expect("rustc");
            assert!(status.success(), "rustc failed on the Rust writer");
            let took = start.elapsed();
            best = Some(best.map_or(took, |b| b.min(took)));
        }
        let run = least(&exe, Some(&expected));
        println!(
            "json {who} build_ms={:.2} bytes={} ns_per_byte={} (with the start of the process and the list)",
            best.unwrap().as_secs_f64() * 1000.0,
            size(&exe),
            per_byte(run, bytes),
        );
        rust_exes.push((who, exe));
    }

    // Instructions for a byte, when valgrind is there. The Smith program's
    // are those of the run less those of the run that does not write.
    let ir = |exe: &Path| instructions(&[exe.as_os_str()], None, work);
    let smith_ir = ir(&work.join("json-whole"))
        .zip(ir(&work.join("json-base")))
        .map(|(whole, base)| (whole.saturating_sub(base) / reps as u64, "smith"));
    let rust_ir = rust_exes
        .iter()
        .filter_map(|(who, exe)| Some((ir(exe)?, *who)));
    for (n, who) in smith_ir.into_iter().chain(rust_ir) {
        println!(
            "json {who} instructions_per_byte={:.1} (of {bytes} bytes, under valgrind)",
            n as f64 / bytes as f64
        );
    }
    profile::print(
        smith,
        work,
        "json",
        ("json-whole", "json-base"),
        reps,
        bytes,
    );
}

/// The least of `RUNS` builds of the Smith program.
fn build_time(smith: &Path, work: &Path, name: &str) -> Duration {
    let file = work.join(format!("{name}-whole.smith"));
    let mut best: Option<Duration> = None;
    for _ in 0..RUNS {
        let start = Instant::now();
        let status = Command::new(smith)
            .arg("build")
            .arg(&file)
            .current_dir(work)
            .status()
            .unwrap();
        assert!(status.success(), "smith build {} failed", file.display());
        let took = start.elapsed();
        best = Some(best.map_or(took, |b| b.min(took)));
    }
    best.unwrap()
}

/// The Smith program that reads the text `reps` times, which builds the list
/// and writes it each time and does `body` with the text.
fn smith_reader(reps: usize, body: &str) -> String {
    format!(
        "use std.bits (bit_and, i32, u32)
use std.console (console, print)
use std.deserialize (Problem, ReadError)
use std.json (from_json, to_json)
use std.list (push)

struct Item {{
  id: u32
  count: i32
  on: bool
}}

fn main() ! console, alloc {{
  var last: Nat = 0
  loop k in 0..{reps} {{
    var xs: List[Item] = []
    loop i in 0..{ITEMS} {{ xs = push(xs, Item(id: i, count: -5, on: bit_and(i, 1) == 0)) }}
    t = to_json(xs)
    {body}
  }}
  print(\"{{last}}\")
}}
"
    )
}

/// The same in Rust: the text is written by hand, and read by hand into a
/// vector of structs, which names its field, checks its number, and says where
/// the text is wrong, as the Smith reader does for the same list.
const RUST_READER: &str = r#"use std::hint::black_box;

struct Item {
    id: u32,
    count: i32,
    on: bool,
}

fn number(out: &mut Vec<u8>, n: i64) {
    if n < 0 {
        out.push(b'-');
    }
    let mut n = n.unsigned_abs();
    let mut digits = [0u8; 20];
    let mut at = digits.len();
    loop {
        at -= 1;
        digits[at] = b'0' + (n % 10) as u8;
        n /= 10;
        if n == 0 {
            break;
        }
    }
    out.extend_from_slice(&digits[at..]);
}

fn write(items: &[Item]) -> Vec<u8> {
    let mut out = Vec::new();
    out.push(b'[');
    for (i, item) in items.iter().enumerate() {
        if i > 0 {
            out.push(b',');
        }
        out.extend_from_slice(b"{\"id\":");
        number(&mut out, item.id as i64);
        out.extend_from_slice(b",\"count\":");
        number(&mut out, item.count as i64);
        out.extend_from_slice(b",\"on\":");
        out.extend_from_slice(if item.on { b"true" } else { b"false" });
        out.push(b'}');
    }
    out.push(b']');
    out
}

struct Reader<'a> {
    bytes: &'a [u8],
    at: usize,
}

type Failed = usize;

impl Reader<'_> {
    fn space(&mut self) {
        while matches!(self.bytes.get(self.at), Some(b' ' | b'\t' | b'\n' | b'\r')) {
            self.at += 1;
        }
    }

    fn expect(&mut self, b: u8) -> Result<(), Failed> {
        self.space();
        if self.bytes.get(self.at) == Some(&b) {
            self.at += 1;
            Ok(())
        } else {
            Err(self.at)
        }
    }

    fn peek(&mut self) -> Option<u8> {
        self.space();
        self.bytes.get(self.at).copied()
    }

    fn key(&mut self) -> Result<&[u8], Failed> {
        self.expect(b'"')?;
        let start = self.at;
        while self.bytes.get(self.at).is_some_and(|b| *b != b'"') {
            self.at += 1;
        }
        let key = &self.bytes[start..self.at];
        self.expect(b'"')?;
        self.expect(b':')?;
        Ok(key)
    }

    fn integer(&mut self) -> Result<i64, Failed> {
        self.space();
        let start = self.at;
        let negative = self.bytes.get(self.at) == Some(&b'-');
        if negative {
            self.at += 1;
        }
        let mut n: i64 = 0;
        let digits = self.at;
        while let Some(d) = self.bytes.get(self.at).filter(|b| b.is_ascii_digit()) {
            n = n.checked_mul(10).and_then(|n| n.checked_add((d - b'0') as i64)).ok_or(start)?;
            self.at += 1;
        }
        if self.at == digits {
            return Err(start);
        }
        Ok(if negative { -n } else { n })
    }

    fn boolean(&mut self) -> Result<bool, Failed> {
        self.space();
        for (word, value) in [(&b"true"[..], true), (&b"false"[..], false)] {
            if self.bytes[self.at..].starts_with(word) {
                self.at += word.len();
                return Ok(value);
            }
        }
        Err(self.at)
    }

    fn item(&mut self) -> Result<Item, Failed> {
        self.expect(b'{')?;
        let (mut id, mut count, mut on) = (None, None, None);
        loop {
            let at = self.at;
            match self.key()? {
                b"id" if id.is_none() => id = Some(u32::try_from(self.integer()?).map_err(|_| at)?),
                b"count" if count.is_none() => count = Some(i32::try_from(self.integer()?).map_err(|_| at)?),
                b"on" if on.is_none() => on = Some(self.boolean()?),
                _ => return Err(at),
            }
            match self.peek() {
                Some(b',') => self.at += 1,
                Some(b'}') => {
                    self.at += 1;
                    break;
                }
                _ => return Err(self.at),
            }
        }
        match (id, count, on) {
            (Some(id), Some(count), Some(on)) => Ok(Item { id, count, on }),
            _ => Err(self.at),
        }
    }

    fn items(&mut self) -> Result<Vec<Item>, Failed> {
        self.expect(b'[')?;
        let mut items = Vec::new();
        if self.peek() == Some(b']') {
            self.at += 1;
            return Ok(items);
        }
        loop {
            items.push(self.item()?);
            match self.peek() {
                Some(b',') => self.at += 1,
                Some(b']') => {
                    self.at += 1;
                    break;
                }
                _ => return Err(self.at),
            }
        }
        self.space();
        if self.at == self.bytes.len() { Ok(items) } else { Err(self.at) }
    }
}

fn main() {
    let mut items = Vec::new();
    for i in 0..ITEMS as u32 {
        items.push(Item { id: i, count: -5, on: i & 1 == 0 });
    }
    let text = write(&items);
    let mut last = 0;
    for _ in 0..REPS {
        let mut reader = Reader { bytes: black_box(&text), at: 0 };
        last = reader.items().unwrap().len();
    }
    println!("{last}");
}
"#;

/// Reading the text that `write` makes: the Smith reader and the Rust one, as
/// the time and the instructions for each byte of the text, the time to build
/// each, and the size of each executable. What each program does beyond
/// reading, which is building the list and writing it, is taken away by the
/// same program that does not read.
fn read(smith: &Path, work: &Path) {
    let bytes = text_size();
    let expected = ITEMS.to_string();
    let (time, reps) = smith_time(smith, work, "jsonread", &expected, |reps| {
        (
            smith_reader(
                reps,
                "r: Result[List[Item], ReadError] = from_json(t)\n    match r {\n      .ok(w) { last = w.size }\n      .err(e) { last = 0 }\n    }",
            ),
            smith_reader(reps, "last = t.size"),
        )
    });
    let built = build_time(smith, work, "jsonread");
    println!(
        "jsonread smith build_ms={:.2} bytes={} ns_per_byte={}",
        built.as_secs_f64() * 1000.0,
        size(&work.join("jsonread-whole")),
        per_byte(time, bytes),
    );

    // The Rust reader, as is and with `-O`, and the same that does not read.
    let mut rust_exes = Vec::new();
    for (who, flags) in [("rust", &[][..]), ("rust_O", &["-O"][..])] {
        let exe = |tag: &str| work.join(format!("jsonread_{who}_{tag}"));
        let mut best: Option<Duration> = None;
        for (tag, reads) in [("whole", 1), ("base", 0)] {
            let main = work.join(format!("jsonread_{who}_{tag}.rs"));
            std::fs::write(
                &main,
                format!(
                    "const ITEMS: usize = {ITEMS};\nconst REPS: usize = {reads};\n{RUST_READER}"
                ),
            )
            .unwrap();
            for _ in 0..RUNS {
                let start = Instant::now();
                let status = Command::new("rustc")
                    .args(["--edition", "2024", "-o"])
                    .arg(exe(tag))
                    .args(flags)
                    .arg(&main)
                    .stderr(Stdio::null())
                    .status()
                    .expect("rustc");
                assert!(status.success(), "rustc failed on the Rust reader");
                if tag == "whole" {
                    let took = start.elapsed();
                    best = Some(best.map_or(took, |b| b.min(took)));
                }
            }
        }
        let net = least(&exe("whole"), Some(&expected))
            .zip(least(&exe("base"), None))
            .map(|(whole, base)| whole.saturating_sub(base));
        println!(
            "jsonread {who} build_ms={:.2} bytes={} ns_per_byte={}",
            best.unwrap().as_secs_f64() * 1000.0,
            size(&exe("whole")),
            per_byte(net, bytes),
        );
        rust_exes.push((who, exe("whole"), exe("base")));
    }

    // Instructions for a byte, when valgrind is there: those of the run less
    // those of the run that does not read.
    let ir = |exe: &Path| instructions(&[exe.as_os_str()], None, work);
    let smith_ir = ir(&work.join("jsonread-whole"))
        .zip(ir(&work.join("jsonread-base")))
        .map(|(whole, base)| (whole.saturating_sub(base) / reps as u64, "smith"));
    let rust_ir = rust_exes
        .iter()
        .filter_map(|(who, whole, base)| Some((ir(whole)?.saturating_sub(ir(base)?), *who)));
    for (n, who) in smith_ir.into_iter().chain(rust_ir) {
        println!(
            "jsonread {who} instructions_per_byte={:.1} (of {bytes} bytes, under valgrind)",
            n as f64 / bytes as f64
        );
    }
    profile::print(
        smith,
        work,
        "jsonread",
        ("jsonread-whole", "jsonread-base"),
        reps,
        bytes,
    );
}
