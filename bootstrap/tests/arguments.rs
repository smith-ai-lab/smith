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
        "a_word_that_is_not_valid_utf8_ends_the_program_before_main_with_a_line_that_says_which",
        a_word_that_is_not_valid_utf8_ends_the_program_before_main_with_a_line_that_says_which,
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
    (
        "smith_never_panics_on_an_argument_of_its_own_that_is_not_valid_utf8",
        smith_never_panics_on_an_argument_of_its_own_that_is_not_valid_utf8,
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
        "every_executable_started_with_a_word_that_is_not_valid_utf8_ends_before_main",
        every_executable_started_with_a_word_that_is_not_valid_utf8_ends_before_main,
    ),
    (
        "run_gives_the_program_the_words_after_the_files_name",
        run_gives_the_program_the_words_after_the_files_name,
    ),
    (
        "run_does_what_the_executable_does_for_a_word_that_is_not_valid_utf8",
        run_does_what_the_executable_does_for_a_word_that_is_not_valid_utf8,
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

/// What a program that takes its words does when the first word that is not
/// valid UTF-8 is the one numbered `n` from 1: nothing is printed, one line
/// says which, and the status is 1.
fn refused(n: usize) -> (Outcome, Vec<u8>) {
    let line = format!("argument {n} is not valid UTF-8\n").into_bytes();
    (
        Outcome {
            stdout: Vec::new(),
            status: 1,
        },
        line,
    )
}

/// Bytes that make a word valid or not by where they fall: the edges of every
/// range a character of UTF-8 is made of, and a few that are plain.
const EDGES: &[u8] = &[
    b'a', 0x7f, 0x80, 0x8f, 0x90, 0x9f, 0xa0, 0xbf, 0xc0, 0xc1, 0xc2, 0xdf, 0xe0, 0xe1, 0xec, 0xed,
    0xee, 0xef, 0xf0, 0xf1, 0xf3, 0xf4, 0xf5, 0xff,
];

/// A list of words made of bytes, none of them 0, which a word cannot hold.
fn byte_words(g: &mut Gen) -> Vec<Vec<u8>> {
    let count = g.below(4);
    (0..count)
        .map(|_| {
            let length = g.below(6);
            (0..length)
                .map(|_| {
                    if g.one_in(4) {
                        1 + g.below(255) as u8
                    } else {
                        *g.pick(EDGES)
                    }
                })
                .collect()
        })
        .collect()
}

/// The place from 1 of the first word that is not valid UTF-8, if there is one.
fn first_invalid(words: &[Vec<u8>]) -> Option<usize> {
    words
        .iter()
        .position(|w| std::str::from_utf8(w).is_err())
        .map(|i| i + 1)
}

fn a_word_that_is_not_valid_utf8_ends_the_program_before_main_with_a_line_that_says_which() {
    let each = program(EACH);
    generate::check(4, 400, byte_words, |words| {
        let said = scalar::run_with_words_and_errors(&each, words);
        match first_invalid(words) {
            None => assert_eq!(said, (ended(lines(words)), Vec::new())),
            Some(n) => assert_eq!(said, refused(n)),
        }
    });
    // A command line of Windows holds a half of a pair of UTF-16 with no
    // other half as a word that is not valid, and no byte is replaced.
    let line: Vec<u16> = "p ok ".encode_utf16().chain([0xd800]).collect();
    assert_eq!(
        scalar::run_with_command_line_and_errors(&each, &line),
        refused(2)
    );
    let line: Vec<u16> = "p ok ".encode_utf16().chain([0xdc00, 0x41]).collect();
    assert_eq!(
        scalar::run_with_command_line_and_errors(&each, &line),
        refused(2)
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

/// A word that is not valid, as this system allows one.
fn invalid_word() -> std::ffi::OsString {
    #[cfg(unix)]
    {
        use std::os::unix::ffi::OsStringExt;
        std::ffi::OsString::from_vec(vec![b'x', 0xff, 0xc0])
    }
    #[cfg(windows)]
    {
        use std::os::windows::ffi::OsStringExt;
        std::ffi::OsString::from_wide(&[0x78, 0xd800])
    }
}

/// Runs the executable, started with the words, from a file of its own, and
/// gives everything it did, standard error too.
pub fn output_of(bytes: &[u8], words: &[std::ffi::OsString]) -> std::process::Output {
    use std::sync::atomic::{AtomicUsize, Ordering};
    static COUNT: AtomicUsize = AtomicUsize::new(0);
    let dir = std::env::temp_dir().join(format!(
        "smith-run-{}-{}",
        std::process::id(),
        COUNT.fetch_add(1, Ordering::Relaxed) + 7000
    ));
    std::fs::create_dir_all(&dir).unwrap();
    let path = dir.join("program");
    crate::executable::write(&path, bytes);
    let output = crate::clock::output_within(std::process::Command::new(&path).args(words), true)
        .unwrap();
    std::fs::remove_dir_all(&dir).unwrap();
    output
}

fn every_executable_started_with_a_word_that_is_not_valid_utf8_ends_before_main() {
    let said = refused(2);
    for target in runnable() {
        let bytes = build(EACH.as_bytes(), target).unwrap_or_else(|e| panic!("{e:?}"));
        if fingerprinting() {
            record(target, &[&bytes, &said.1]);
            continue;
        }
        let words = ["fine".into(), invalid_word(), "later".into()];
        let output = output_of(&bytes, &words);
        let shown = format!("{}: {output:?}", target.name());
        assert_eq!(output.status.code(), Some(1), "{shown}");
        assert_eq!(output.stdout, said.0.stdout, "{shown}");
        assert_eq!(output.stderr, said.1, "{shown}");
    }
}

/// `smith` started in the folder with the arguments, which may be any bytes
/// the system allows.
fn smith_with(dir: &std::path::Path, args: &[std::ffi::OsString]) -> std::process::Output {
    std::process::Command::new(env!("CARGO_BIN_EXE_smith"))
        .args(args)
        .current_dir(dir)
        .output()
        .unwrap()
}

fn smith_never_panics_on_an_argument_of_its_own_that_is_not_valid_utf8() {
    let dir = folder("invalid_arguments");
    std::fs::write(dir.join("prog.smith"), EACH).unwrap();
    let invalid = invalid_word();
    let cases: Vec<Vec<std::ffi::OsString>> = vec![
        vec!["build".into(), invalid.clone()],
        vec!["run".into(), invalid.clone()],
        vec!["check".into(), invalid.clone()],
        vec!["build".into(), "--target".into(), invalid.clone()],
        vec![
            "build".into(),
            "--flag".into(),
            "prog.smith".into(),
            invalid.clone(),
        ],
        vec![invalid.clone()],
    ];
    for args in cases {
        let ran = smith_with(&dir, &args);
        let shown = format!("{args:?}: {ran:?}");
        assert_eq!(ran.status.code(), Some(1), "{shown}");
        assert!(ran.stdout.is_empty(), "{shown}");
        let errors = String::from_utf8_lossy(&ran.stderr);
        assert!(!errors.contains("panicked"), "{shown}");
    }
    // A file's name that is not valid is an error in the usual form.
    let ran = smith_with(&dir, &["build".into(), invalid]);
    assert!(
        stderr(&ran).contains("is not valid UTF-8"),
        "{}",
        stderr(&ran)
    );
}

fn run_does_what_the_executable_does_for_a_word_that_is_not_valid_utf8() {
    if fingerprinting() {
        return targets::record_for_every_target(EACH.as_bytes());
    }
    let Some(host) = Target::host().filter(|host| runnable().contains(host)) else {
        return;
    };
    let dir = folder("run_invalid_word");
    std::fs::write(dir.join("prog.smith"), EACH).unwrap();
    let words: [std::ffi::OsString; 3] = ["fine".into(), invalid_word(), "later".into()];
    let bytes = build(EACH.as_bytes(), host).unwrap_or_else(|e| panic!("{e:?}"));
    let built = output_of(&bytes, &words);
    let mut args: Vec<std::ffi::OsString> = vec!["run".into(), "prog.smith".into()];
    args.extend(words);
    let ran = smith_with(&dir, &args);
    assert_eq!(ran.status.code(), built.status.code(), "{ran:?}");
    assert_eq!(ran.stdout, built.stdout);
    assert_eq!(ran.stderr, built.stderr);
    assert_eq!(ran.stderr, refused(2).1);
    assert_eq!(ran.status.code(), Some(1));
}
