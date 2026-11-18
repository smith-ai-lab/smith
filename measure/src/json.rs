//! `std.json` writing a list of 100,000 small structs, beside the same in
//! Rust written by hand, with no library: the time and the instructions for
//! each byte of the text, and the time to build each program.

use std::path::Path;
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

use crate::build::{instructions, per_byte, size};
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
        "use std.json (to_json)

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
    let built = build_time(smith, work);
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
}

/// The least of `RUNS` builds of the Smith program.
fn build_time(smith: &Path, work: &Path) -> Duration {
    let file = work.join("json-whole.smith");
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
