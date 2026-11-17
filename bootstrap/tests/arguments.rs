//! The words a program is started with, which `main` may take as a
//! `List[str]`.
//!
//! The words are drawn from printable ASCII, with the spaces, quotes, and
//! backslashes that a command line treats as more than themselves. What each
//! program prints is worked out in Rust from the words alone, so it shares
//! nothing with the compiler.

use smith::scalar::{self, Outcome};
use smith::target::Target;
use smith::{compile, lower};

use crate::Test;
use crate::cli::{folder, smith, stderr};
use crate::generate::{self, Gen};
use crate::targets::{self, build, fingerprinting, record, runnable};

pub const ONCE: &[Test] = &[
    (
        "main_is_given_the_words_after_the_programs_name_and_not_the_name",
        main_is_given_the_words_after_the_programs_name_and_not_the_name,
    ),
    (
        "main_is_given_every_list_of_words_in_order_by_the_evaluator",
        main_is_given_every_list_of_words_in_order_by_the_evaluator,
    ),
    (
        "a_command_line_of_windows_is_split_as_the_systems_own_programs_split_it",
        a_command_line_of_windows_is_split_as_the_systems_own_programs_split_it,
    ),
    (
        "a_command_line_of_windows_reads_quotes_and_backslashes_as_the_c_runtime_does",
        a_command_line_of_windows_reads_quotes_and_backslashes_as_the_c_runtime_does,
    ),
    (
        "a_command_line_of_windows_is_given_to_main_as_utf8",
        a_command_line_of_windows_is_given_to_main_as_utf8,
    ),
    (
        "a_main_with_a_parameter_that_is_not_a_list_of_text_is_refused_and_the_error_says_what_it_may_take",
        a_main_with_a_parameter_that_is_not_a_list_of_text_is_refused_and_the_error_says_what_it_may_take,
    ),
    (
        "a_main_with_no_parameter_reads_and_asks_for_nothing_for_the_words",
        a_main_with_no_parameter_reads_and_asks_for_nothing_for_the_words,
    ),
];

pub const MACHINE: &[Test] = &[
    (
        "every_executable_is_started_with_the_words_it_is_given",
        every_executable_is_started_with_the_words_it_is_given,
    ),
    (
        "every_executable_counts_the_words_it_is_started_with",
        every_executable_counts_the_words_it_is_started_with,
    ),
    (
        "run_gives_the_program_the_words_after_the_files_name",
        run_gives_the_program_the_words_after_the_files_name,
    ),
];

/// The program of the issue: it counts its words.
const COUNT: &str = "fn main(args: List[str]) ! console, alloc {
  print(\"started with {args.size} arguments\")
}
";

/// A program that prints each word on a line of its own.
const EACH: &str = "fn main(args: List[str]) ! console, alloc {
  loop i in 0..args.size {
    print(args[i])
  }
}
";

fn words(texts: &[&str]) -> Vec<Vec<u8>> {
    texts.iter().map(|t| t.as_bytes().to_vec()).collect()
}

/// What `EACH` prints for the words: each on a line of its own.
fn lines(words: &[Vec<u8>]) -> Vec<u8> {
    words
        .iter()
        .flat_map(|w| w.iter().copied().chain(std::iter::once(b'\n')))
        .collect()
}

fn ended(stdout: Vec<u8>) -> Outcome {
    Outcome { stdout, status: 0 }
}

fn program(source: &str) -> scalar::Program {
    let lowered = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
    assert_eq!(scalar::check(&lowered), Ok(()));
    scalar::passes(&lowered)
}

/// The characters a word may hold: the spaces, quotes, and backslashes that
/// a command line treats as more than themselves come first, as they are the
/// most likely to find a mistake, and then the rest of printable ASCII.
fn character(g: &mut Gen) -> u8 {
    if g.one_in(3) {
        *g.pick(b" \"\\")
    } else {
        *g.pick(&(0x20..0x7f).collect::<Vec<u8>>())
    }
}

/// A list of words, each of printable ASCII, and none to a few of them. A
/// word may be empty.
fn word_list(g: &mut Gen) -> Vec<Vec<u8>> {
    let count = g.below(5);
    (0..count)
        .map(|_| {
            let length = g.below(7);
            (0..length).map(|_| character(g)).collect()
        })
        .collect()
}

fn main_is_given_the_words_after_the_programs_name_and_not_the_name() {
    let counting = program(COUNT);
    let said = |words: &[Vec<u8>]| scalar::run_with_words(&counting, words).stdout;
    assert_eq!(said(&words(&["one", "two"])), b"started with 2 arguments\n");
    assert_eq!(said(&[]), b"started with 0 arguments\n");
}

fn main_is_given_every_list_of_words_in_order_by_the_evaluator() {
    let each = program(EACH);
    generate::check(1, 200, word_list, |words| {
        assert_eq!(scalar::run_with_words(&each, words), ended(lines(words)));
    });
}

/// The command line a program is started with on Windows when it is given
/// the words, as the C runtime's rules read it back: a word with a space, a
/// tab, or nothing in it is in quotes, a quote has a backslash before it, and
/// the backslashes before a quote, or before the closing quote of a word,
/// are twice as many.
fn command_line(name: &str, words: &[Vec<u8>]) -> Vec<u16> {
    let mut line: Vec<u8> = name.as_bytes().to_vec();
    for word in words {
        line.push(b' ');
        let quoted = word.is_empty() || word.iter().any(|c| *c == b' ' || *c == b'\t');
        if quoted {
            line.push(b'"');
        }
        let mut slashes = 0;
        for &c in word {
            if c == b'\\' {
                slashes += 1;
            } else {
                if c == b'"' {
                    line.extend(std::iter::repeat_n(b'\\', slashes + 1));
                }
                slashes = 0;
            }
            line.push(c);
        }
        if quoted {
            line.extend(std::iter::repeat_n(b'\\', slashes));
            line.push(b'"');
        }
    }
    String::from_utf8(line).unwrap().encode_utf16().collect()
}

fn a_command_line_of_windows_is_split_as_the_systems_own_programs_split_it() {
    let each = program(EACH);
    let names = [
        "program.exe",
        "\"C:\\Program Files\\program.exe\"",
        "C:\\bin\\program",
    ];
    let case = |g: &mut Gen| (*g.pick(&names), word_list(g));
    generate::check(2, 200, case, |(name, words)| {
        let line = command_line(name, words);
        assert_eq!(
            scalar::run_with_command_line(&each, &line),
            ended(lines(words)),
            "{}",
            String::from_utf16_lossy(&line)
        );
    });
}

fn a_command_line_of_windows_reads_quotes_and_backslashes_as_the_c_runtime_does() {
    let each = program(EACH);
    // Each is a command line, and the words that the rules of the C runtime
    // read in it: the ones its documentation gives, and the ends of them.
    let cases: [(&str, &[&str]); 16] = [
        (r#"p"#, &[]),
        (r#"p "#, &[]),
        ("p   a \t b  ", &["a", "b"]),
        (r#"p "a b" c"#, &["a b", "c"]),
        (r#"p a\\b"#, &[r#"a\\b"#]),
        (r#"p "a\\\\" b"#, &[r#"a\\"#, "b"]),
        (r#"p a\"b c d"#, &[r#"a"b"#, "c", "d"]),
        (r#"p a\\\"b c d"#, &[r#"a\"b"#, "c", "d"]),
        (r#"p a\\\\"b c" d e"#, &[r#"a\\b c"#, "d", "e"]),
        (r#"p "a ""b"" c""#, &[r#"a "b" c"#]),
        (r#"p """#, &[""]),
        (r#"p a "" b"#, &["a", "", "b"]),
        (r#"p "a b"#, &["a b"]),
        (r#"p a\"#, &[r#"a\"#]),
        (r#""C:\x y\p.exe" q"#, &["q"]),
        ("", &[]),
    ];
    for (line, expected) in cases {
        let units: Vec<u16> = line.encode_utf16().collect();
        assert_eq!(
            scalar::run_with_command_line(&each, &units),
            ended(lines(&words(expected))),
            "{line}"
        );
    }
}

fn a_command_line_of_windows_is_given_to_main_as_utf8() {
    let each = program(EACH);
    // A character of one, two, and three units' worth of bytes, and one that
    // takes two units, which is two halves.
    let texts = [
        "caf\u{e9}",
        "\u{20ac}5",
        "\u{1f600}",
        "a \u{1f600} b",
        "\u{7ff}\u{800}\u{ffff}\u{10000}\u{10ffff}",
    ];
    let words = words(&texts);
    let line = command_line("program.exe", &words);
    assert_eq!(
        scalar::run_with_command_line(&each, &line),
        ended(lines(&words))
    );
    // A pair of halves is one character, and not two, wherever it is in a word.
    let units: Vec<u16> = "program.exe \"x\u{1f600}\" \u{1f600}\u{1f600}"
        .encode_utf16()
        .collect();
    assert_eq!(
        scalar::run_with_command_line(&each, &units).stdout,
        "x\u{1f600}\n\u{1f600}\u{1f600}\n".as_bytes()
    );
}

/// The errors a source gives, one to a line, as the command shows them.
fn errors_of(source: &str) -> Vec<String> {
    match lower(source.as_bytes()) {
        Ok(_) => Vec::new(),
        Err(errors) => errors.iter().map(|e| format!("{e:?}")).collect(),
    }
}

fn a_main_with_a_parameter_that_is_not_a_list_of_text_is_refused_and_the_error_says_what_it_may_take()
 {
    let dir = folder("main_parameters");
    let refused = [
        "fn main(args: List[u8]) ! console {}\n",
        "fn main(args: str) ! console {}\n",
        "fn main(args: List[List[str]]) ! console {}\n",
        "fn main(count: u8) ! console {}\n",
        "fn main(args: List[str], more: List[str]) ! console {}\n",
        "fn main(args: List[str] = []) ! console {}\n",
    ];
    for (i, source) in refused.iter().enumerate() {
        assert!(!errors_of(source).is_empty(), "{source}");
        let file = format!("refused{i}.smith");
        std::fs::write(dir.join(&file), source).unwrap();
        let built = smith(&dir, &["check", &file]);
        assert!(!built.status.success(), "{source}");
        assert!(
            stderr(&built).contains("`main` takes no parameter, or one, the words the program was started with, as a `List[str]`"),
            "{source}: {}",
            stderr(&built)
        );
    }
    // These are the two forms `main` may take, and neither is refused.
    for source in [COUNT, EACH, "fn main() ! console { print(\"hi\") }\n"] {
        assert_eq!(errors_of(source), Vec::<String>::new(), "{source}");
    }
}

fn a_main_with_no_parameter_reads_and_asks_for_nothing_for_the_words() {
    // The name of what a Windows program asks the system for the words with,
    // which its executable holds when it asks.
    let asks = |bytes: &[u8]| bytes.windows(15).any(|w| w == b"GetCommandLineW");
    for source in [
        "fn main() ! console { print(\"hello world\") }\n",
        "fn main() ! console, alloc {\n  x = \"a\"\n  print(\"{x}{x}\")\n}\n",
    ] {
        let program = program(source);
        assert!(!program.takes_arguments());
        let main = &program.functions[program.main];
        assert!(main.blocks[0].params.is_empty());
        for target in Target::ALL {
            let bytes = compile(source.as_bytes(), *target).unwrap();
            assert!(!asks(&bytes), "{} asks for words", target.name());
        }
    }
    // A program that takes them asks the system for them, and only then.
    assert!(program(EACH).takes_arguments());
    for target in Target::ALL {
        let bytes = compile(EACH.as_bytes(), *target).unwrap();
        assert_eq!(
            asks(&bytes),
            bytes.starts_with(b"MZ"),
            "{} asks for words",
            target.name()
        );
    }
}

/// What the executable of `source` prints for the words, for each target this
/// machine runs, checked against what the evaluator says, or, for a
/// fingerprint, noted with it.
fn executables_agree(source: &str, words: &[Vec<u8>], said: &Outcome) {
    let strings: Vec<String> = words
        .iter()
        .map(|w| String::from_utf8(w.clone()).unwrap())
        .collect();
    for target in runnable() {
        let bytes = build(source.as_bytes(), target).unwrap_or_else(|e| panic!("{e:?}"));
        if fingerprinting() {
            let given = words.concat();
            record(target, &[&bytes, &given, &said.stdout]);
        } else {
            assert_eq!(
                targets::run_with_words(&bytes, &strings),
                *said,
                "{} with {strings:?}",
                target.name()
            );
        }
    }
}

fn every_executable_is_started_with_the_words_it_is_given() {
    let each = program(EACH);
    generate::check(3, 40, word_list, |words| {
        let said = scalar::run_with_words(&each, words);
        assert_eq!(said, ended(lines(words)));
        executables_agree(EACH, words, &said);
    });
}

fn every_executable_counts_the_words_it_is_started_with() {
    let counting = program(COUNT);
    for texts in [&["one", "two"][..], &[]] {
        let words = words(texts);
        let said = scalar::run_with_words(&counting, &words);
        assert_eq!(
            said.stdout,
            format!("started with {} arguments\n", words.len()).as_bytes()
        );
        executables_agree(COUNT, &words, &said);
    }
}

fn run_gives_the_program_the_words_after_the_files_name() {
    if fingerprinting() {
        return targets::record_for_every_target(COUNT.as_bytes());
    }
    if !Target::host().is_some_and(|host| runnable().contains(&host)) {
        return;
    }
    let dir = folder("run_words");
    std::fs::write(dir.join("prog.smith"), COUNT).unwrap();
    std::fs::write(dir.join("each.smith"), EACH).unwrap();
    let ran = smith(&dir, &["run", "prog.smith", "one", "two"]);
    assert_eq!(ran.status.code(), Some(0), "{}", stderr(&ran));
    assert_eq!(ran.stdout, b"started with 2 arguments\n");
    let ran = smith(&dir, &["run", "prog.smith"]);
    assert_eq!(ran.stdout, b"started with 0 arguments\n");
    // What follows the file's name is the program's, even when it looks like
    // a flag of `smith`, and a flag before the file's name is `smith`'s.
    let ran = smith(
        &dir,
        &["run", "--timings", "each.smith", "--target", "a b", ""],
    );
    assert_eq!(ran.status.code(), Some(0), "{}", stderr(&ran));
    assert_eq!(ran.stdout, b"--target\na b\n\n");
    // `build` takes no words.
    let built = smith(&dir, &["build", "prog.smith", "one"]);
    assert!(!built.status.success());
}
