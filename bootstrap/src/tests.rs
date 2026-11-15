//! Properties of the compiler, checked in-process on generated programs.

use crate::compile;
use crate::diag::{self, Error, Pos};
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

/// The place just after `text`, which starts at the start of a file.
fn pos_after(text: &str) -> Pos {
    let line = text.matches('\n').count() + 1;
    Pos {
        line,
        col: col_after(text),
    }
}

fn errors(source: &str) -> Vec<Error> {
    match compile(source.as_bytes(), Target::LinuxX64) {
        Ok(_) => panic!("expected an error for {source:?}"),
        Err(errors) => errors,
    }
}

/// The first error reported for a source.
fn error(source: &str) -> Error {
    errors(source).remove(0)
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
                .unwrap_or_else(|e| panic!("{e:?} in {source}"));
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
        if let Err(errors) = compile(&source, Target::LinuxX64) {
            assert_well_formed(&source, &errors);
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
    let e = &compile(&source, Target::LinuxX64).unwrap_err()[0];
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

/// Checks what must hold of the errors for any input: there is at least
/// one, each is at a place inside the file, they are in the order of their
/// places with none twice, advice is never in a message, and the printed
/// form holds no control characters other than line breaks.
fn assert_well_formed(source: &[u8], errors: &[Error]) {
    let shown = String::from_utf8_lossy(source);
    let text = diag::text(source);
    let lines: Vec<&str> = text.split('\n').collect();
    assert!(!errors.is_empty(), "no error for {shown:?}");
    for e in errors {
        let Pos { line, col } = e.pos;
        assert!(line >= 1 && line <= lines.len(), "{e} for {shown:?}");
        let length = lines[line - 1].chars().count();
        assert!(col >= 1 && col <= length + 1, "{e} for {shown:?}");
        assert!(!e.message.contains("write `"), "{e} holds its fix");
    }
    for pair in errors.windows(2) {
        assert!(pair[0].pos <= pair[1].pos, "out of order for {shown:?}");
        assert_ne!(pair[0], pair[1], "reported twice for {shown:?}");
    }
    let printed = diag::render("f.smith", source, errors);
    assert!(
        printed.chars().all(|c| c == '\n' || !c.is_control()),
        "{printed:?}"
    );
    let fixes = printed.lines().filter(|l| l.starts_with("  fix: ")).count();
    assert_eq!(fixes, errors.iter().filter(|e| e.fix.is_some()).count());
}

/// A piece of a string literal: how it is written, how many places it takes
/// on a terminal, and the error it holds, if any.
type Piece = (&'static str, usize, Option<&'static str>);

const GOOD_PIECES: &[Piece] = &[
    ("a", 1, None),
    ("hello", 5, None),
    (" ", 1, None),
    ("\t", 1, None),
    ("é", 1, None),
    ("e\u{301}", 1, None),
    ("日本", 4, None),
    ("🙂", 2, None),
    ("\\\"", 2, None),
    ("\\\\", 2, None),
    ("\\{", 2, None),
    ("\\n", 2, None),
];

const BAD_PIECES: &[Piece] = &[
    ("\\q", 2, Some("unknown escape")),
    ("\\0", 2, Some("unknown escape")),
    ("\\ ", 2, Some("unknown escape")),
    ("\\é", 2, Some("unknown escape")),
    ("}", 1, Some("closes no expression")),
    ("{x}", 3, Some("not supported yet")),
];

/// A file whose only mistakes are in the tokens, each at a known place,
/// along with what each error must mention. It also holds a mistake for the
/// checker, if `main` lacks `console`, which must not be reported.
fn file_with_token_errors(rng: &mut Rng) -> (String, Vec<(Pos, &'static str)>) {
    let mut source = String::new();
    let mut expected = Vec::new();
    source.push_str(rng.pick(&["fn main() ! console {", "fn main() {"]));
    source.push_str(rng.pick(&[" ", "\n  ", "\n\t"]));
    if rng.below(3) == 0 {
        expected.push((pos_after(&source), "unexpected character"));
        source.push_str(rng.pick(&["#", "$", "?"]));
    }
    source.push_str("print(\"");
    for _ in 0..1 + rng.below(8) {
        let (written, _, bad) = if rng.below(2) == 0 {
            rng.pick(BAD_PIECES)
        } else {
            rng.pick(GOOD_PIECES)
        };
        if let Some(what) = bad {
            expected.push((pos_after(&source), *what));
        }
        source.push_str(written);
    }
    if expected
        .iter()
        .all(|(_, what)| *what == "unexpected character")
    {
        expected.push((pos_after(&source), "closes no expression"));
        source.push('}');
    }
    source.push_str("\")");
    source.push_str(" }");
    if rng.below(3) == 0 {
        source.push(' ');
        expected.push((pos_after(&source), "own line"));
        source.push_str("// note");
    }
    source.push('\n');
    (source, expected)
}

/// A file whose mistakes are all in its syntax, one in each statement or
/// definition that holds one, at known places. Its `main` lacks `console`,
/// which must not be reported.
fn file_with_syntax_errors(rng: &mut Rng) -> (String, Vec<(Pos, &'static str)>) {
    const STATEMENTS: &[(&str, &str, Option<&str>)] = &[
        ("", "print(\"a\")", None),
        ("", "x = 1", Some("not supported yet")),
        ("", "var x = 1", Some("not supported yet")),
        ("print(\"a\" ", "\"b\")", Some("`,` or `)`")),
        ("print(\"a\")", ".x", Some("not supported yet")),
        ("", ")", Some("statement")),
    ];
    const ITEMS: &[(&str, &str)] = &[
        ("struct A { x: f32 }", "not supported yet"),
        ("use std.console (input)", "not supported yet"),
        (")", "definition"),
        ("fn f() ! console { print(\"a\") }", "after the first"),
        ("fn g()\n{\n  x = 1\n}", "after the first"),
    ];
    loop {
        let mut source = String::new();
        let mut expected = Vec::new();
        let items = rng.below(3);
        let main_at = rng.below(items + 1);
        for i in 0..=items {
            if i == main_at {
                source.push_str("fn main() {\n");
                for n in 0..1 + rng.below(4) {
                    source.push_str("  ");
                    let (before, rest, bad) = rng.pick(STATEMENTS);
                    let start = pos_after(&source);
                    source.push_str(before);
                    if let Some(what) = bad {
                        expected.push((pos_after(&source), *what));
                    } else if n > 0 {
                        expected.push((start, "after the first"));
                    }
                    source.push_str(rest);
                    source.push('\n');
                }
                source.push_str("}\n");
            } else {
                let (text, what) = rng.pick(ITEMS);
                if i < main_at && text.starts_with("fn") {
                    continue;
                }
                expected.push((pos_after(&source), *what));
                source.push_str(text);
                source.push('\n');
            }
        }
        if !expected.is_empty() {
            return (source, expected);
        }
    }
}

/// A file whose mistakes are all for the checker, with the errors it must
/// give.
fn file_with_check_errors(rng: &mut Rng) -> (String, Vec<(Pos, String)>) {
    loop {
        let mut source = String::from("fn main()");
        let mut expected = Vec::new();
        let mut listed: Vec<&str> = Vec::new();
        let mut unknown = false;
        for i in 0..rng.below(4) {
            source.push_str(if i == 0 { " ! " } else { ", " });
            let effect = *rng.pick(&["console", "fs", "alloc", "consol", "net"]);
            let at = pos_after(&source);
            source.push_str(effect);
            if listed.contains(&effect) {
                expected.push((at, format!("`{effect}` effect is already listed")));
                continue;
            }
            listed.push(effect);
            match effect {
                "console" => {}
                "consol" => {
                    unknown = true;
                    expected.push((at, "no effect named `consol`".to_string()));
                }
                _ => expected.push((at, format!("the `{effect}` effect is not supported yet"))),
            }
        }
        source.push_str(" {\n  ");
        let callee = *rng.pick(&["print", "print", "prin", "main"]);
        let at = pos_after(&source);
        source.push_str(callee);
        source.push('(');
        let args = rng.below(3);
        for i in 0..args {
            if i == 1 {
                source.push_str(", ");
                if callee == "print" {
                    expected.push((pos_after(&source), "`end` and `to`".to_string()));
                }
            }
            source.push_str("\"a\"");
        }
        match callee {
            "print" => {
                if args == 0 {
                    expected.push((pos_after(&source), "needs the text".to_string()));
                }
                if !listed.contains(&"console") && !unknown {
                    expected.push((at, "`console` effect".to_string()));
                }
            }
            "main" => expected.push((at, "not supported yet".to_string())),
            _ => expected.push((at, "no function named `prin`".to_string())),
        }
        source.push_str(")\n}\n");
        if !expected.is_empty() {
            expected.sort_by_key(|(pos, _)| *pos);
            return (source, expected);
        }
    }
}

fn assert_errors(source: &str, expected: &[(Pos, &str)]) {
    let found = errors(source);
    assert_well_formed(source.as_bytes(), &found);
    let places: Vec<Pos> = found.iter().map(|e| e.pos).collect();
    let wanted: Vec<Pos> = expected.iter().map(|(pos, _)| *pos).collect();
    assert_eq!(places, wanted, "{found:?} for {source:?}");
    for (e, (_, what)) in found.iter().zip(expected) {
        assert!(e.message.contains(what), "{e} lacks {what} in {source:?}");
    }
}

#[test]
fn every_error_of_the_tokens_is_reported_once_in_order() {
    let mut rng = Rng::new(5);
    for _ in 0..500 {
        let (source, expected) = file_with_token_errors(&mut rng);
        assert_errors(&source, &expected);
    }
}

#[test]
fn every_error_of_the_syntax_is_reported_once_in_order() {
    let mut rng = Rng::new(6);
    for _ in 0..500 {
        let (source, expected) = file_with_syntax_errors(&mut rng);
        assert_errors(&source, &expected);
    }
}

#[test]
fn every_error_of_the_checker_is_reported_once_in_order() {
    let mut rng = Rng::new(7);
    for _ in 0..500 {
        let (source, expected) = file_with_check_errors(&mut rng);
        let expected: Vec<(Pos, &str)> = expected.iter().map(|(p, w)| (*p, w.as_str())).collect();
        assert_errors(&source, &expected);
    }
}

#[test]
fn an_error_shows_its_line_with_a_mark_under_its_column() {
    let mut rng = Rng::new(8);
    for _ in 0..500 {
        let blank = *rng.pick(&["", "\n", "\n\n\n\n\n\n\n\n\n\n"]);
        let indent = *rng.pick(&["", "  ", "\t", " \t "]);
        let head = format!("fn main() ! console {{{blank}\n{indent}print(\"");
        let mut source = head.clone();
        let mut places = 1 + indent.len() + "print(\"".len();
        for _ in 0..rng.below(8) {
            let (written, width, _) = rng.pick(GOOD_PIECES);
            source.push_str(written);
            places += width;
        }
        let at = pos_after(&source);
        source.push_str("\\q\") }\n");
        let line = source.lines().nth(at.line - 1).unwrap();
        let printed = diag::render("f.smith", source.as_bytes(), &errors(&source));
        let number = at.line.to_string();
        let gutter = " ".repeat(number.len());
        let expected = format!(
            "f.smith:{number}:{}: unknown escape: a backslash is followed by `q`, \
             not one of \" \\ {{ }} n t\n  {number} | {}\n  {gutter} | {}^\n",
            at.col,
            line.replace('\t', " "),
            " ".repeat(places - 1),
        );
        assert_eq!(printed, expected, "for {source:?}");
    }
}

#[test]
fn advice_is_on_the_fix_line_and_not_in_the_message() {
    let cases: &[(&[u8], &str)] = &[
        (b"fn main() { print(\"a\") }", "`fn main() ! console`"),
        (
            b"fn main() ! fs { print(\"a\") }",
            "`fn main() ! fs, console`",
        ),
        (b"fn main() ! console { print(\"}\") }", "`\\}`"),
        (b"fn main() ! console { print() }", "`print(\"hello\")`"),
        (b"fn main() ! console {\r\n}", "line feed"),
    ];
    for (source, fix) in cases {
        let found = compile(source, Target::LinuxX64).unwrap_err();
        assert_well_formed(source, &found);
        let e = found.iter().find(|e| e.fix.is_some()).expect("a fix");
        assert!(e.fix.as_ref().unwrap().contains(fix), "{e:?}");
        let printed = diag::render("f.smith", source, &found);
        assert!(printed.contains("  fix: "), "{printed}");
    }
    let found = errors("fn main() ! console { prin(\"a\") }");
    assert!(found.iter().all(|e| e.fix.is_none()));
    let printed = diag::render("f.smith", b"fn main() ! console { prin(\"a\") }", &found);
    assert!(!printed.contains("fix:"), "{printed}");
}

#[test]
fn a_mistake_in_the_text_hides_every_later_stage() {
    let parts: &[&[u8]] = &[
        b"fn main() {",
        b"\r",
        b"\n  print(\"\\q",
        b"\xff",
        b"\")",
        b"\r",
        b"\n}",
        b"\r",
        b"\n",
    ];
    let mut source = Vec::new();
    let mut expected = Vec::new();
    for part in parts {
        if matches!(part, [b'\r'] | [0xff]) {
            expected.push(pos_after(&diag::text(&source)));
        }
        source.extend_from_slice(part);
    }
    let found = compile(&source, Target::LinuxX64).unwrap_err();
    assert_well_formed(&source, &found);
    let places: Vec<_> = found.iter().map(|e| e.pos).collect();
    assert_eq!(places, expected);
    source.retain(|b| *b != b'\r' && *b != 0xff);
    let found = compile(&source, Target::LinuxX64).unwrap_err();
    assert_eq!(found.len(), 1);
    assert!(found[0].message.contains("unknown escape"));
}
