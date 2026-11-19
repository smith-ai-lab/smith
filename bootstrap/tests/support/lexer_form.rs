//! The lexer of `compiler/lexer.smith` and the Rust lexer, each run on text
//! and printed in one plain form, so that the two are compared line by line.
//! Nothing here is a test.

use std::sync::OnceLock;

/// The lexer written in Smith, without the `main` it ships with.
fn lexer() -> &'static str {
    static LEXER: OnceLock<String> = OnceLock::new();
    LEXER.get_or_init(|| {
        let whole = include_str!("../../../compiler/lexer.smith");
        let cut = whole.find("\nfn main()").expect("lexer.smith has a main");
        whole[..cut + 1].to_string()
    })
}

/// What a program that lexes each input prints for it. Each input starts with
/// a line `input`.
const DRIVER: &str = r#"
fn show(b: List[Byte]) -> str ! alloc {
  var s: str = ""
  loop i in 0..b.size {
    if i == 0 { s = "{b[i]}" } else { s = "{s},{b[i]}" }
  }
  s
}

fn show_token(t: Token) ! console, alloc {
  l = t.pos.line
  c = t.pos.col
  match t.tok {
    .ident(b) { print("ident {l} {c} {show(b)}") }
    .keyword(k) { print("keyword {l} {c} {k}") }
    .number(b) { print("number {l} {c} {show(b)}") }
    .string(b) { print("string {l} {c} {show(b)}") }
    .interp_open { print("interp_open {l} {c}") }
    .text(b) { print("text {l} {c} {show(b)}") }
    .interp_close { print("interp_close {l} {c}") }
    .punct(k) { print("punct {l} {c} {k}") }
    .newline { print("newline {l} {c}") }
    .eof { print("eof {l} {c}") }
  }
}

fn show_error(e: LexError) ! console, alloc {
  print("error {e.pos.line} {e.pos.col} {e.code} {e.why} {show(e.detail)}")
}

fn main() ! console, alloc {
  texts = inputs()
  loop k in 0..texts.size {
    print("input")
    match lex(texts[k]) {
      .ok(tokens) {
        loop i in 0..tokens.size { show_token(tokens[i]) }
      }
      .err(errors) {
        loop i in 0..errors.size { show_error(errors[i]) }
      }
    }
  }
}
"#;

/// A program that lexes each of the texts, with the lexer written in Smith.
pub fn program(texts: &[&[u8]]) -> String {
    let mut out = String::from(lexer());
    out.push_str(DRIVER);
    out.push_str("\nfn nothing() -> List[Byte] ! alloc {\n  []\n}\n");
    out.push_str("\nfn inputs() -> List[List[Byte]] ! alloc {\n  [");
    for (i, text) in texts.iter().enumerate() {
        if i > 0 {
            out.push_str(", ");
        }
        // A list that holds no byte has no type of its own.
        if text.is_empty() {
            out.push_str("nothing()");
            continue;
        }
        let bytes: Vec<String> = text.iter().map(|b| b.to_string()).collect();
        out.push_str(&format!("[{}]", bytes.join(", ")));
    }
    out.push_str("]\n}\n");
    out
}

/// What the program of `program` prints when the lexer written in Smith
/// agrees with the Rust lexer: a line `input` and then the lines of the Rust
/// lexer, for each text.
pub fn expected(texts: &[&[u8]]) -> String {
    let mut out = String::new();
    for text in texts {
        out.push_str("input\n");
        for line in rust(text) {
            out.push_str(&line);
            out.push('\n');
        }
    }
    out
}

/// What the Rust lexer says of a text, in the same form.
pub fn rust(text: &[u8]) -> Vec<String> {
    match smith::lex_plain(text) {
        Ok(tokens) => tokens.lines().map(str::to_string).collect(),
        Err(errors) => errors
            .iter()
            .map(|e| format!("error {} {} {}", e.pos.line, e.pos.col, code(&e.message)))
            .collect(),
    }
}

/// The bytes of a character an error shows, as `U+00E9` or between backticks.
fn shown(text: &str) -> String {
    let c = match text.strip_prefix("U+") {
        Some(hex) => char::from_u32(u32::from_str_radix(hex, 16).unwrap()).unwrap(),
        None => text.trim_matches('`').chars().next().unwrap(),
    };
    bytes(c.to_string().as_bytes())
}

fn bytes(b: &[u8]) -> String {
    let b: Vec<String> = b.iter().map(|x| x.to_string()).collect();
    b.join(",")
}

/// What the lexer written in Smith says for an error of the Rust lexer: the
/// number of its kind, why, and the bytes it names.
fn code(message: &str) -> String {
    let fixed = |code: u32| format!("{code} 0 ");
    match message {
        "a carriage return is not allowed in a Smith file" => return fixed(1),
        "the file is not valid UTF-8 here" => return fixed(2),
        "a comment must stand on its own line" => return fixed(3),
        "this string has no closing quote on the line it starts" => return fixed(4),
        "a character has no closing `'`" => return fixed(7),
        _ => {}
    }
    if message.starts_with("a `}` in a string closes") {
        return fixed(5);
    }
    if message.starts_with("a character is written between") {
        return fixed(8);
    }
    if message.starts_with("a character has no closing `'`, and") {
        return fixed(10);
    }
    if let Some(rest) = message.strip_prefix("unknown escape: a backslash is followed by ") {
        let (after, allowed) = rest.split_once(", not one of ").unwrap();
        let code = if allowed.contains('"') { 6 } else { 9 };
        if after == "nothing" {
            return format!("{code} 1 ");
        }
        return format!("{code} 2 {}", shown(after));
    }
    if let Some(rest) = message.strip_prefix("unexpected character ") {
        return format!("11 0 {}", shown(rest));
    }
    if let Some(rest) = message.strip_prefix('`') {
        let (text, why) = rest.split_once("` is not a number: ").unwrap();
        let base = |name: &str| match name {
            "hexadecimal" => 16,
            "binary" => 2,
            _ => 10,
        };
        let text_bytes = bytes(text.as_bytes());
        if let Some((name, _)) = why.split_once(" digits are needed here") {
            return format!("12 {} {text_bytes},0,0", base(name) * 10 + 1);
        }
        if why.starts_with("an `_`") {
            let b = if text.starts_with("0x") {
                16
            } else if text.starts_with("0b") {
                2
            } else {
                10
            };
            return format!("12 {} {text_bytes},0,0", b * 10 + 2);
        }
        let (shown_char, name) = why.split_once(" is not a ").unwrap();
        let name = name.strip_suffix(" digit").unwrap();
        return format!("12 {} {text_bytes},0,{}", base(name) * 10 + 3, shown(shown_char));
    }
    panic!("an error the lexer written in Smith has no number for: {message}")
}
