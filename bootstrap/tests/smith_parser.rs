//! The parser written in Smith, in `compiler/parser.smith`, as far as the
//! compiler on `main` builds it: the part above the line `// ---- not built`,
//! which reads the `use` lines of a file and steps over the other items. It
//! takes the tokens of the lexer written in Smith, and is held to the Rust
//! parser by what both can be asked.
//!
//! The Rust parser is not public, and its tree can be reached from no test, so
//! the two are compared where they can be: the `use` lines a generator writes
//! are read back as the generator wrote them, with their places, and the
//! mistakes a generator makes in them are the errors that `smith::check` gives,
//! each with its place, what was wanted there, and what was found. Which parts
//! of the parser this covers is the list that ends the comment on issue #675.

use crate::Test;
use crate::generate::{self, Gen};
use crate::targets;

pub const MACHINE: &[Test] = &[
    (
        "the_parser_written_in_smith_reads_the_use_lines_a_generator_writes_as_it_wrote_them",
        the_parser_written_in_smith_reads_the_use_lines_a_generator_writes_as_it_wrote_them,
    ),
    (
        "the_parser_written_in_smith_gives_the_errors_in_use_lines_that_the_rust_parser_gives",
        the_parser_written_in_smith_gives_the_errors_in_use_lines_that_the_rust_parser_gives,
    ),
];

/// How many files each test reads.
const FILES: u64 = 40;

/// The lexer and the parser written in Smith, without the `main` of the lexer
/// and the `use` lines of the parser, which the lexer has already, and the part
/// of the parser that the compiler does not build.
fn parser() -> &'static str {
    static PARSER: std::sync::OnceLock<String> = std::sync::OnceLock::new();
    PARSER.get_or_init(|| {
        let lexer = include_str!("../../compiler/lexer.smith");
        let lexer = &lexer[..lexer.find("\nfn main()").expect("lexer.smith has a main") + 1];
        let parser = include_str!("../../compiler/parser.smith");
        let end = parser
            .find("\n// ---- not built")
            .expect("parser.smith says where what is not built starts");
        let built: String = parser[..end + 1]
            .lines()
            .filter(|line| !line.starts_with("use "))
            .map(|line| format!("{line}\n"))
            .collect();
        format!("{lexer}{built}")
    })
}

/// What a program that reads each input prints for it. Each input starts with a
/// line `input`.
const DRIVER: &str = r#"
fn show(b: List[Byte]) -> str ! alloc {
  var s: str = ""
  loop i in 0..b.size {
    if i == 0 { s = "{b[i]}" } else { s = "{s},{b[i]}" }
  }
  s
}

fn show_name(what: str, n: Name) ! console, alloc {
  print("{what} {n.pos.line} {n.pos.col} {show(n.text)}")
}

fn show_use(u: Use) ! console, alloc {
  l = if u.listed { 1 } else { 0 }
  print("use {u.pos.line} {u.pos.col} {l}")
  loop i in 0..u.module.size { show_name("module", u.module[i]) }
  match u.alias {
    .some(n) { show_name("alias", n) }
    .none { }
  }
  loop i in 0..u.names.size {
    show_name("name", u.names[i].name)
    match u.names[i].alias {
      .some(n) { show_name("as", n) }
      .none { }
    }
  }
}

fn found(t: Tok, keywords: List[List[Byte]], puncts: List[List[Byte]]) -> str ! alloc {
  match t {
    .ident(b) { "text {show(b)}" }
    .keyword(k) { if k < keywords.size { "text {show(keywords[k])}" } else { "text" } }
    .punct(k) { if k < puncts.size { "text {show(puncts[k])}" } else { "text" } }
    .number(b) { "number {show(b)}" }
    .newline { "newline" }
    .eof { "eof" }
    .string(b) { "string" }
    .interp_open { "string" }
    .text(b) { "string" }
    .interp_close { "string" }
  }
}

fn show_error(e: ParseError, keywords: List[List[Byte]], puncts: List[List[Byte]]) ! console, alloc {
  if e.wanted == 8 or e.wanted == 9 {
    print("error {e.pos.line} {e.pos.col} {e.wanted} -")
  } else {
    print("error {e.pos.line} {e.pos.col} {e.wanted} {found(e.found, keywords, puncts)}")
  }
}

fn main() ! console, alloc {
  texts = inputs()
  keywords = keyword_table()
  puncts = punctuation_table()
  loop k in 0..texts.size {
    print("input")
    match lex(texts[k]) {
      .ok(tokens) {
        r = parse_uses(tokens)
        loop i in 0..r.uses.size { show_use(r.uses[i]) }
        loop i in 0..r.errors.size { show_error(r.errors[i], keywords, puncts) }
      }
      .err(errors) { print("lex error") }
    }
  }
}
"#;

/// A program that reads each of the texts with the parser written in Smith.
fn program(texts: &[String]) -> String {
    let mut out = String::from(parser());
    out.push_str(DRIVER);
    out.push_str("\nfn inputs() -> List[List[Byte]] ! alloc {\n  [");
    for (i, text) in texts.iter().enumerate() {
        if i > 0 {
            out.push_str(", ");
        }
        let bytes: Vec<String> = text.bytes().map(|b| b.to_string()).collect();
        out.push_str(&format!("[{}]", bytes.join(", ")));
    }
    out.push_str("]\n}\n");
    out
}

/// What the program prints for each text, as the lines after each `input`.
fn said_of_each(texts: &[String]) -> Vec<Vec<String>> {
    let source = program(texts);
    let mut each: Vec<Vec<Vec<String>>> = Vec::new();
    for target in targets::runnable() {
        let bytes = targets::build(source.as_bytes(), target).unwrap_or_else(|e| panic!("{e:?}"));
        targets::record(target, &[&bytes]);
        if targets::fingerprinting() {
            continue;
        }
        let said = targets::run(&bytes);
        assert_eq!(said.status, 0, "{}", target.name());
        let said = String::from_utf8(said.stdout).unwrap();
        let mut lines: Vec<Vec<String>> = Vec::new();
        for line in said.lines() {
            if line == "input" {
                lines.push(Vec::new());
            } else {
                lines.last_mut().unwrap().push(line.to_string());
            }
        }
        each.push(lines);
    }
    each.into_iter().next().unwrap_or_default()
}

/// A text as it is written, with the place of what is written in it, so that
/// the lines the parser must give are known from how it was written.
#[derive(Default)]
struct Written {
    text: String,
    line: usize,
    col: usize,
    expected: Vec<String>,
}

impl Written {
    fn new() -> Written {
        Written {
            line: 1,
            col: 1,
            ..Written::default()
        }
    }

    /// Writes `s`, which holds no line break.
    fn put(&mut self, s: &str) {
        self.text.push_str(s);
        self.col += s.len();
    }

    fn newline(&mut self) {
        self.text.push('\n');
        self.line += 1;
        self.col = 1;
    }

    /// Writes a name and says what the parser gives for it.
    fn name(&mut self, what: &str, name: &str) {
        let bytes: Vec<String> = name.bytes().map(|b| b.to_string()).collect();
        self.expected
            .push(format!("{what} {} {} {}", self.line, self.col, bytes.join(",")));
        self.put(name);
    }
}

const MODULES: &[&str] = &["std", "list", "console", "bits", "int", "ascii", "text", "db", "model"];
const NAMES: &[&str] = &["push", "print", "console", "u64", "is_digit", "alloc", "x", "_y", "T2"];

/// A name that is neither a keyword nor a punctuation.
fn module(g: &mut Gen) -> &'static str {
    g.pick(MODULES)
}

/// A file that is one or more `use` lines, in the forms of the grammar, and
/// then an item that is no `use`, with what the parser gives for the lines.
fn use_lines(g: &mut Gen) -> Written {
    let mut w = Written::new();
    for _ in 0..1 + g.below(3) {
        let (line, col) = (w.line, w.col);
        let form = g.below(5);
        let head_at = w.expected.len();
        w.put("use ");
        for k in 0..1 + g.below(3) {
            if k > 0 {
                w.put(".");
            }
            w.name("module", module(g));
        }
        let mut listed = 0;
        match form {
            0 => {}
            1 => {
                w.put(" as ");
                w.name("alias", module(g));
            }
            2..=4 => {
                listed = 1;
                w.put(if form == 3 { " (" } else { "(" });
                let many = if form == 4 { 0 } else { 1 + g.below(3) };
                for k in 0..many {
                    if k > 0 {
                        w.put(",");
                        if form == 3 {
                            w.newline();
                            w.put("  ");
                        } else {
                            w.put(" ");
                        }
                    } else if form == 3 {
                        w.newline();
                        w.put("  ");
                    }
                    w.name("name", g.pick(NAMES));
                    if g.one_in(3) {
                        w.put(" as ");
                        w.name("as", g.pick(NAMES));
                    }
                }
                if form == 3 {
                    w.put(",");
                    w.newline();
                }
                w.put(")");
            }
            _ => unreachable!(),
        }
        w.newline();
        // The parser gives the line before what is in it.
        w.expected.insert(head_at, format!("use {line} {col} {listed}"));
    }
    w.newline();
    w.put("fn main() ! console {");
    w.newline();
    w.put("}");
    w.newline();
    w
}

fn the_parser_written_in_smith_reads_the_use_lines_a_generator_writes_as_it_wrote_them() {
    let files: Vec<Written> = (0..FILES).map(|case| generate::sample(51, case, use_lines)).collect();
    let texts: Vec<String> = files.iter().map(|w| w.text.clone()).collect();
    let said = said_of_each(&texts);
    if targets::fingerprinting() {
        return;
    }
    assert_eq!(said.len(), files.len());
    for ((file, said), text) in files.iter().zip(&said).zip(&texts) {
        assert_eq!(&file.expected, said, "on {text:?}");
    }
}

/// What the Rust parser says for an error, in the form the program prints: the
/// line, the column, what was wanted by number, and what was found.
fn rust_error(e: &smith::diag::Error) -> Option<String> {
    let at = format!("error {} {}", e.pos.line, e.pos.col);
    let message = e.message.as_str();
    if message == "`use` comes before every other item of the file" {
        return Some(format!("{at} 9 -"));
    }
    if message == "a list of names after a module's `as` is not supported yet" {
        return Some(format!("{at} 8 -"));
    }
    let (wanted, found) = message.strip_prefix("expected ")?.split_once(", found ")?;
    let wanted = match wanted {
        "the name of a module" => 1,
        "a name after `.`" => 2,
        "a name for the module" => 3,
        "a name to import or `)`" => 4,
        "the name to give it" => 5,
        "`,` or `)`" => 6,
        "a line break" => 7,
        _ => return None,
    };
    let bytes = |s: &str| {
        let b: Vec<String> = s.bytes().map(|b| b.to_string()).collect();
        b.join(",")
    };
    let found = match found {
        "a line break" => "newline".to_string(),
        "the end of the file" => "eof".to_string(),
        "a string" => "string".to_string(),
        _ => match found.strip_prefix("the number `") {
            Some(n) => format!("number {}", bytes(n.trim_end_matches('`'))),
            None => format!("text {}", bytes(found.trim_matches('`'))),
        },
    };
    Some(format!("{at} {wanted} {found}"))
}

/// A file of `use` lines with a mistake made in them: a token taken out, a
/// token put in where it does not belong, or two lines joined.
fn mistaken(g: &mut Gen) -> String {
    let w = use_lines(g);
    let mut text = w.text.into_bytes();
    let end = text.windows(8).position(|s| s == b"fn main(").unwrap_or(text.len());
    for _ in 0..1 + g.below(2) {
        let at = g.below(end.max(1));
        match g.below(3) {
            0 => {
                text.remove(at.min(text.len() - 1));
            }
            1 => {
                let piece: &[u8] = g.pick(&[
                    b" as ".as_slice(),
                    b".",
                    b",",
                    b"(",
                    b")",
                    b" 5 ",
                    b" use ",
                    b" fn ",
                    b" \"a\" ",
                    b"\n",
                ]);
                text.splice(at..at, piece.iter().copied());
            }
            _ => {
                text.splice(at..at, b" x ".iter().copied());
            }
        }
    }
    String::from_utf8(text).unwrap()
}

fn the_parser_written_in_smith_gives_the_errors_in_use_lines_that_the_rust_parser_gives() {
    let texts: Vec<String> = (0..FILES * 4)
        .map(|case| generate::sample(52, case, mistaken))
        // A text that the lexer refuses is for the lexer's test.
        .filter(|text| smith::lex_plain(text.as_bytes()).is_ok())
        .collect();
    let said = said_of_each(&texts);
    if targets::fingerprinting() {
        return;
    }
    let mut mistaken = 0;
    for (text, said) in texts.iter().zip(&said) {
        let said: Vec<&String> = said.iter().filter(|l| l.starts_with("error ")).collect();
        let rust: Vec<String> = match smith::check(text.as_bytes()) {
            Err(errors) => errors.iter().filter_map(rust_error).collect(),
            Ok(_) => Vec::new(),
        };
        mistaken += usize::from(!rust.is_empty());
        assert_eq!(
            said.iter().map(|l| l.as_str()).collect::<Vec<_>>(),
            rust.iter().map(|l| l.as_str()).collect::<Vec<_>>(),
            "on {text:?}"
        );
    }
    assert!(
        mistaken * 4 >= texts.len(),
        "only {mistaken} of {} texts had a mistake for the parsers to find",
        texts.len()
    );
}
