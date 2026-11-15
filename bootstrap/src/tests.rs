//! Properties of the compiler, checked in-process on generated programs.

use crate::compile;
use crate::diag::Error;
use crate::target::Target;

/// A small, fixed random number generator, so that every run of the tests
/// sees the same cases.
struct Rng(u64);

impl Rng {
    fn new(seed: u64) -> Rng {
        Rng(seed.wrapping_mul(0x9e37_79b9_7f4a_7c15) | 1)
    }

    fn next(&mut self) -> u64 {
        self.0 ^= self.0 << 13;
        self.0 ^= self.0 >> 7;
        self.0 ^= self.0 << 17;
        self.0
    }

    fn below(&mut self, n: usize) -> usize {
        (self.next() % n as u64) as usize
    }

    fn pick<'a, T>(&mut self, items: &'a [T]) -> &'a T {
        &items[self.below(items.len())]
    }
}

const PREFIX: &str = "fn main() ! console { print(";
const SUFFIX: &str = ") }\n";

fn program(literal: &str) -> String {
    format!("{PREFIX}{literal}{SUFFIX}")
}

/// A valid string literal, written as source, and the text it stands for.
fn literal(rng: &mut Rng) -> (String, Vec<u8>) {
    const PIECES: &[(&str, &str)] = &[
        ("a", "a"),
        ("hello", "hello"),
        (" ", " "),
        ("\t", "\t"),
        ("é", "é"),
        ("日本", "日本"),
        ("🙂", "🙂"),
        ("'", "'"),
        ("//", "//"),
        ("!@#$%^&*()", "!@#$%^&*()"),
        ("\\\"", "\""),
        ("\\\\", "\\"),
        ("\\{", "{"),
        ("\\}", "}"),
        ("\\n", "\n"),
        ("\\t", "\t"),
    ];
    let mut source = String::from("\"");
    let mut text = Vec::new();
    for _ in 0..rng.below(12) {
        let (written, means) = rng.pick(PIECES);
        source.push_str(written);
        text.extend_from_slice(means.as_bytes());
    }
    source.push('"');
    (source, text)
}

/// The column of the first character after `text`, on its last line.
fn col_after(text: &str) -> usize {
    text.rsplit('\n').next().unwrap_or("").chars().count() + 1
}

fn error(source: &str) -> Error {
    match compile(source.as_bytes(), Target::LinuxX64) {
        Ok(_) => panic!("expected an error for {source:?}"),
        Err(e) => e,
    }
}

fn assert_error(source: &str, line: usize, col: usize, contains: &str) {
    let e = error(source);
    assert_eq!(
        (e.pos.line, e.pos.col),
        (line, col),
        "position of {e} in {source:?}"
    );
    assert!(e.message.contains(contains), "{e} in {source:?}");
}

#[cfg(all(target_os = "linux", target_arch = "x86_64"))]
mod native {
    use super::*;
    use std::sync::atomic::{AtomicUsize, Ordering};

    /// Runs an executable and returns its status and standard output.
    pub fn run(bytes: &[u8]) -> (i32, Vec<u8>) {
        static COUNT: AtomicUsize = AtomicUsize::new(0);
        let dir = std::env::temp_dir().join(format!(
            "smith-unit-{}-{}",
            std::process::id(),
            COUNT.fetch_add(1, Ordering::Relaxed)
        ));
        std::fs::create_dir_all(&dir).unwrap();
        let path = dir.join("program");
        crate::write_executable(&path, bytes).unwrap();
        // Another test's process may briefly hold the file open while it
        // starts, which makes running it fail as busy.
        let mut tries = 0;
        let output = loop {
            match std::process::Command::new(&path).output() {
                Err(e) if e.raw_os_error() == Some(26) && tries < 100 => {
                    tries += 1;
                    std::thread::sleep(std::time::Duration::from_millis(10));
                }
                result => break result.unwrap(),
            }
        };
        std::fs::remove_dir_all(&dir).unwrap();
        (output.status.code().unwrap_or(-1), output.stdout)
    }

    #[test]
    fn hello_world_prints_a_line_and_exits_with_0() {
        let bytes = compile(
            b"fn main() ! console { print(\"hello world\") }\n",
            Target::LinuxX64,
        )
        .unwrap();
        assert_eq!(run(&bytes), (0, b"hello world\n".to_vec()));
    }

    #[test]
    fn a_literal_prints_the_text_it_stands_for() {
        let mut rng = Rng::new(1);
        for _ in 0..150 {
            let (source, text) = literal(&mut rng);
            let bytes = compile(program(&source).as_bytes(), Target::LinuxX64)
                .unwrap_or_else(|e| panic!("{e} in {source}"));
            let mut expected = text;
            expected.push(b'\n');
            assert_eq!(run(&bytes), (0, expected), "for {source}");
        }
    }

    #[test]
    fn a_long_literal_is_printed_whole() {
        let text = "abcdefghij".repeat(100_000);
        let bytes = compile(program(&format!("\"{text}\"")).as_bytes(), Target::LinuxX64).unwrap();
        let (status, out) = run(&bytes);
        assert_eq!(status, 0);
        assert_eq!(out, format!("{text}\n").into_bytes());
    }
}

#[test]
fn an_invalid_literal_is_an_error_at_its_place() {
    let mut rng = Rng::new(2);
    for _ in 0..300 {
        let (before, _) = literal(&mut rng);
        let (after, _) = literal(&mut rng);
        let open = &before[..before.len() - 1];
        let head = format!("{PREFIX}{open}");
        let (bad, at, what) = match rng.below(5) {
            0 => (
                format!("\\{}", rng.pick(&["q", "x", "0", " ", "u", "é", "'"])),
                col_after(&head),
                "unknown escape",
            ),
            1 => ("}".to_string(), col_after(&head), "closes no expression"),
            2 => ("\n".to_string(), col_after(PREFIX), "no closing quote"),
            3 => ("\\\n".to_string(), col_after(&head), "unknown escape"),
            _ => ("{x}".to_string(), col_after(&head), "not supported yet"),
        };
        let source = format!("{head}{bad}{}{SUFFIX}", &after[1..]);
        assert_error(&source, 1, at, what);
    }
}

#[test]
fn a_literal_with_no_closing_quote_is_an_error_at_its_quote() {
    assert_error(
        "fn main() ! console {\n  print(\"abc",
        2,
        9,
        "no closing quote",
    );
}

#[test]
fn every_input_gives_an_executable_or_an_error_with_a_place() {
    let seeds = [
        "fn main() ! console { print(\"hello world\") }\n",
        "// greet\nfn main() ! console {\n  print(\n    \"a\\n\\{b\\}\",\n  )\n}\n",
        "fn main() ! console, alloc { print(\"x\", end: \"\") }",
        "struct A { x: f32 }\nfn main() -> i32 { 1 + 2 }",
    ];
    let noise: &[&[u8]] = &[
        b"\"",
        b"{",
        b"}",
        b"(",
        b")",
        b"\\",
        b"\n",
        b" ",
        b"//",
        b",",
        b"!",
        b"fn",
        b"main",
        b"print",
        b"console",
        b":",
        b"=",
        b"0",
        b"\xff",
        b"\xc3",
        b"\xe6\x97\xa5",
        b"\t",
        b"\r",
    ];
    let mut rng = Rng::new(3);
    for i in 0..5000 {
        let mut source = rng.pick(&seeds).as_bytes().to_vec();
        for _ in 0..1 + rng.below(4) {
            let at = rng.below(source.len() + 1);
            match rng.below(3) {
                0 if at < source.len() => {
                    source.remove(at);
                }
                1 => {
                    let piece = rng.pick(noise);
                    source.splice(at..at, piece.iter().copied());
                }
                _ => source.truncate(at),
            }
        }
        if i % 10 == 0 {
            source = (0..rng.below(64)).map(|_| rng.next() as u8).collect();
        }
        if let Err(e) = compile(&source, Target::LinuxX64) {
            let lines = source.iter().filter(|b| **b == b'\n').count() + 1;
            assert!(
                e.pos.line >= 1 && e.pos.line <= lines && e.pos.col >= 1,
                "{e} for {:?}",
                String::from_utf8_lossy(&source)
            );
        }
    }
}

#[test]
fn building_twice_gives_identical_bytes() {
    let mut rng = Rng::new(4);
    for _ in 0..100 {
        let (source, _) = literal(&mut rng);
        let source = program(&source);
        let first = compile(source.as_bytes(), Target::LinuxX64).unwrap();
        let second = compile(source.as_bytes(), Target::LinuxX64).unwrap();
        assert_eq!(first, second);
    }
}

#[test]
fn print_without_console_names_the_missing_effect() {
    assert_error("fn main() { print(\"hello\") }", 1, 13, "`console` effect");
    assert_error(
        "fn main()\n{\n  print(\"hello\")\n}",
        3,
        3,
        "`console` effect",
    );
}

#[test]
fn a_feature_not_built_yet_is_an_error_that_says_so() {
    let cases: &[(&str, usize, usize)] = &[
        ("fn main() ! console { print(\"a {x}\") }", 1, 32),
        ("fn main() ! console { print(\"a\", end: \"\") }", 1, 34),
        ("fn main() ! console { print(\"a\", \"\") }", 1, 34),
        ("fn main() ! console { print(text: \"a\") }", 1, 29),
        ("fn main() ! console { print(1) }", 1, 29),
        ("fn main() ! console { print(x) }", 1, 29),
        ("fn main() ! console { print(\"a\" + \"b\") }", 1, 33),
        ("fn main() ! console { print(\"a\").x }", 1, 33),
        (
            "fn main() ! console {\n  print(\"a\")\n  print(\"b\")\n}",
            3,
            3,
        ),
        ("fn main() ! console {}", 1, 22),
        ("fn main() ! console { x = 1 }", 1, 23),
        ("fn main() ! console { var x = 1 }", 1, 23),
        ("fn main() ! console { main() }", 1, 23),
        ("fn main(x: i32) ! console { print(\"a\") }", 1, 9),
        ("fn main() -> i32 { 0 }", 1, 11),
        ("fn main[T]() ! console { print(\"a\") }", 1, 8),
        ("fn main() ! console\nrequires 1 { print(\"a\") }", 2, 1),
        ("fn main() ! console, alloc { print(\"a\") }", 1, 22),
        ("fn main() ! fs { print(\"a\") }", 1, 13),
        ("struct A { x: f32 }", 1, 1),
        (
            "use std.console (input)\nfn main() ! console { print(\"a\") }",
            1,
            1,
        ),
        ("pub fn main() ! console { print(\"a\") }", 1, 1),
        ("fn main() ! console { print(\"a\") }\nfn f() {}", 2, 1),
    ];
    for (source, line, col) in cases {
        assert_error(source, *line, *col, "not supported yet");
    }
}

#[test]
fn other_mistakes_are_errors_with_a_place() {
    let cases: &[(&str, usize, usize, &str)] = &[
        ("", 1, 1, "no function named `main`"),
        ("// nothing\n", 1, 1, "no function named `main`"),
        ("fn hello() ! console { print(\"a\") }", 1, 1, "`main`"),
        ("fn main() ! console { prin(\"a\") }", 1, 23, "`prin`"),
        ("fn main() ! console { print() }", 1, 29, "needs the text"),
        ("fn main() ! consol { print(\"a\") }", 1, 13, "`consol`"),
        (
            "fn main() ! console { print(\"a\") } // hi",
            1,
            36,
            "own line",
        ),
        ("fn main() ! console { print(\"a\") ", 1, 34, "`}`"),
        ("fn main() ! console { print(\"a\" }", 1, 33, "`,` or `)`"),
        ("fn main( ! console { print(\"a\") }", 1, 10, "`)`"),
        ("fn () ! console { print(\"a\") }", 1, 4, "name"),
        (
            "fn main() ! console { print(\"a\") } x",
            1,
            36,
            "line break",
        ),
        ("fn main() ! console { ) }", 1, 23, "statement"),
        (
            "fn main() ! console { print(\"a\") }\n)",
            2,
            1,
            "definition",
        ),
        (
            "fn main() ! console { print(\"é\") }\n\u{7f}",
            2,
            1,
            "character",
        ),
        ("fn main() ! console { print(\"a\") }\n#", 2, 1, "character"),
    ];
    for (source, line, col, contains) in cases {
        assert_error(source, *line, *col, contains);
    }
}

#[test]
fn a_carriage_return_is_an_error_wherever_it_is() {
    let cases: &[(&str, usize, usize)] = &[
        ("fn main() ! console { print(\"a\rb\") }", 1, 31),
        ("fn main() ! console {\r\n  print(\"a\")\r\n}\r\n", 1, 22),
        (
            "// a comment\r\nfn main() ! console { print(\"a\") }",
            1,
            13,
        ),
        ("fn main() ! console {\n  print(\"a\")\n}\n\r", 4, 1),
    ];
    for (source, line, col) in cases {
        assert_error(source, *line, *col, "carriage return");
    }
}

#[test]
fn an_effect_named_twice_is_an_error_at_the_second_name() {
    assert_error(
        "fn main() ! console, console { print(\"x\") }",
        1,
        22,
        "already listed",
    );
}

#[test]
fn invalid_utf8_is_an_error_at_its_place() {
    let mut source = b"fn main() ! console {\n  print(\"ab".to_vec();
    source.push(0xff);
    source.extend_from_slice(b"\") }");
    let e = compile(&source, Target::LinuxX64).unwrap_err();
    assert_eq!((e.pos.line, e.pos.col), (2, 12));
}

/// The files that come before the target seam.
const BEFORE_THE_SEAM: &[(&str, &str)] = &[
    ("diag.rs", include_str!("diag.rs")),
    ("lexer.rs", include_str!("lexer.rs")),
    ("parser.rs", include_str!("parser.rs")),
    ("check.rs", include_str!("check.rs")),
    ("ir.rs", include_str!("ir.rs")),
];

#[test]
fn nothing_before_the_target_seam_names_a_system_or_machine() {
    for (file, text) in BEFORE_THE_SEAM {
        let text = text.to_lowercase();
        for word in ["linux", "x86", "x64", "syscall", "system call"] {
            assert!(!text.contains(word), "{file} mentions {word}");
        }
    }
}

#[test]
fn only_smith_run_starts_another_program() {
    let mut files = BEFORE_THE_SEAM.to_vec();
    files.push(("target/mod.rs", include_str!("target/mod.rs")));
    files.push(("target/linux_x64.rs", include_str!("target/linux_x64.rs")));
    for (file, text) in files {
        assert!(!text.contains("Command"), "{file} starts a program");
    }
    let main = include_str!("main.rs");
    let starts: Vec<_> = main.match_indices("Command::").collect();
    assert_eq!(starts.len(), 1, "main.rs starts programs in one place");
    let run = main.find("\nfn run(").expect("main.rs has fn run");
    let next = main[run + 1..]
        .find("\nfn ")
        .map_or(main.len(), |i| run + 1 + i);
    assert!(
        (run..next).contains(&starts[0].0),
        "only run starts a program"
    );
}
