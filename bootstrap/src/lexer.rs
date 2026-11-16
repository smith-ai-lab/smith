//! Turns source text into tokens, following the lexical rules in
//! `knowledge/grammar.md`.

use crate::diag::{Error, Pos};

/// The keywords of Smith.
const KEYWORDS: &[&str] = &[
    "and", "as", "break", "derive", "effect", "else", "ensures", "enum", "fn", "for", "foreign",
    "if", "impl", "in", "loop", "match", "not", "or", "out", "pub", "requires", "some", "struct",
    "take", "trait", "try", "use", "var", "with", "without",
];

/// Punctuation and operators, longest first so that the first match is the
/// longest one.
const PUNCTUATION: &[&str] = &[
    "..", "->", "+=", "-=", "*=", "/=", "==", "!=", "<=", ">=", "(", ")", "[", "]", "{", "}", ",",
    ":", ".", "=", "<", ">", "+", "-", "*", "/", "@", "^", "!",
];

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Tok {
    Ident(String),
    Keyword(&'static str),
    /// A number, kept in its source spelling.
    Number(String),
    /// A string literal with its escapes applied.
    Str(Vec<u8>),
    Punct(&'static str),
    Newline,
    Eof,
}

impl Tok {
    /// How the token is named in an error.
    pub fn describe(&self) -> String {
        match self {
            Tok::Ident(name) => format!("`{name}`"),
            Tok::Keyword(word) => format!("`{word}`"),
            Tok::Number(text) => format!("the number `{text}`"),
            Tok::Str(_) => "a string".to_string(),
            Tok::Punct(p) => format!("`{p}`"),
            Tok::Newline => "a line break".to_string(),
            Tok::Eof => "the end of the file".to_string(),
        }
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Token {
    pub tok: Tok,
    pub pos: Pos,
}

/// Splits a file into tokens. The last token is always `Tok::Eof`.
///
/// Every error is reported. If the text itself is not valid Smith source,
/// because of invalid UTF-8 or a carriage return, only those errors are, as
/// the tokens of such a file mean nothing.
pub fn lex(bytes: &[u8]) -> Result<Vec<Token>, Vec<Error>> {
    let mut errors = Vec::new();
    let mut pos = Pos::START;
    for chunk in bytes.utf8_chunks() {
        for c in chunk.valid().chars() {
            if c == '\r' {
                errors.push(
                    Error::new(pos, "a carriage return is not allowed in a Smith file")
                        .with_fix("end lines with a line feed alone"),
                );
            }
            advance(&mut pos, c);
        }
        if !chunk.invalid().is_empty() {
            errors.push(Error::new(pos, "the file is not valid UTF-8 here"));
            pos.col += 1;
        }
    }
    if !errors.is_empty() {
        return Err(errors);
    }
    let text = std::str::from_utf8(bytes).unwrap_or_default();
    Lexer::new(text).run()
}

fn advance(pos: &mut Pos, c: char) {
    if c == '\n' {
        pos.line += 1;
        pos.col = 1;
    } else {
        pos.col += 1;
    }
}

struct Lexer<'a> {
    rest: &'a str,
    pos: Pos,
    tokens: Vec<Token>,
    errors: Vec<Error>,
    /// Whether a token has been read on the current line.
    line_has_token: bool,
}

impl<'a> Lexer<'a> {
    fn new(text: &'a str) -> Lexer<'a> {
        Lexer {
            rest: text,
            pos: Pos::START,
            tokens: Vec::new(),
            errors: Vec::new(),
            line_has_token: false,
        }
    }

    fn peek(&self) -> Option<char> {
        self.rest.chars().next()
    }

    fn peek_second(&self) -> Option<char> {
        self.rest.chars().nth(1)
    }

    fn bump(&mut self) -> Option<char> {
        let c = self.peek()?;
        self.rest = &self.rest[c.len_utf8()..];
        advance(&mut self.pos, c);
        Some(c)
    }

    fn push(&mut self, tok: Tok, pos: Pos) {
        self.line_has_token = tok != Tok::Newline;
        self.tokens.push(Token { tok, pos });
    }

    fn error(&mut self, error: Error) {
        self.errors.push(error);
    }

    fn run(mut self) -> Result<Vec<Token>, Vec<Error>> {
        while let Some(c) = self.peek() {
            let start = self.pos;
            match c {
                ' ' | '\t' => {
                    self.bump();
                }
                '\n' => {
                    self.bump();
                    self.push(Tok::Newline, start);
                }
                '/' if self.peek_second() == Some('/') => self.comment(start),
                '"' => self.string(start),
                'a'..='z' | 'A'..='Z' | '_' => self.word(start),
                '0'..='9' => self.number(start),
                _ => self.punctuation(start, c),
            }
        }
        let end = self.pos;
        self.push(Tok::Eof, end);
        if self.errors.is_empty() {
            Ok(self.tokens)
        } else {
            Err(self.errors)
        }
    }

    fn comment(&mut self, start: Pos) {
        if self.line_has_token {
            self.error(Error::new(start, "a comment must stand on its own line"));
        }
        self.take_while(|c| c != '\n');
    }

    fn take_while(&mut self, keep: impl Fn(char) -> bool) -> &'a str {
        let before = self.rest;
        while let Some(c) = self.peek() {
            if !keep(c) {
                break;
            }
            self.bump();
        }
        &before[..before.len() - self.rest.len()]
    }

    fn word(&mut self, start: Pos) {
        let word = self.take_while(|c| c.is_ascii_alphanumeric() || c == '_');
        let tok = match KEYWORDS.iter().find(|k| **k == word) {
            Some(keyword) => Tok::Keyword(keyword),
            None => Tok::Ident(word.to_string()),
        };
        self.push(tok, start);
    }

    fn number(&mut self, start: Pos) {
        let before = self.rest;
        let word = |c: char| c.is_ascii_alphanumeric() || c == '_';
        self.take_while(word);
        if self.peek() == Some('.') && self.peek_second().is_some_and(|c| c.is_ascii_digit()) {
            self.bump();
            self.take_while(word);
        }
        let text = &before[..before.len() - self.rest.len()];
        self.push(Tok::Number(text.to_string()), start);
    }

    fn punctuation(&mut self, start: Pos, c: char) {
        let Some(p) = PUNCTUATION.iter().find(|p| self.rest.starts_with(**p)) else {
            self.bump();
            self.error(Error::new(
                start,
                format!("unexpected character {}", show_char(c)),
            ));
            return;
        };
        for _ in 0..p.len() {
            self.bump();
        }
        self.push(Tok::Punct(p), start);
    }

    /// Reads a string literal. Each mistake inside it is reported, and the
    /// string still becomes a token, so that the rest of the line is read.
    fn string(&mut self, start: Pos) {
        self.bump();
        let mut value = Vec::new();
        loop {
            let at = self.pos;
            match self.peek() {
                None | Some('\n') => {
                    self.error(Error::new(
                        start,
                        "this string has no closing quote on the line it starts",
                    ));
                    break;
                }
                Some('"') => {
                    self.bump();
                    break;
                }
                Some('{') => {
                    self.error(Error::unsupported(at, "an expression in a string"));
                    if !self.skip_expression() {
                        break;
                    }
                }
                Some('}') => {
                    self.bump();
                    self.error(
                        Error::new(at, "a `}` in a string closes no expression")
                            .with_fix("write `\\}` for a brace"),
                    );
                }
                Some('\\') => {
                    self.bump();
                    let decoded = match self.peek() {
                        Some('"') => b'"',
                        Some('\\') => b'\\',
                        Some('{') => b'{',
                        Some('}') => b'}',
                        Some('n') => b'\n',
                        Some('t') => b'\t',
                        other => {
                            let after = match other {
                                None | Some('\n') => "nothing".to_string(),
                                Some(c) => show_char(c),
                            };
                            self.error(Error::new(
                                at,
                                format!(
                                    "unknown escape: a backslash is followed by {after}, \
                                     not one of \" \\ {{ }} n t"
                                ),
                            ));
                            match other {
                                // The string ends at the line, and that is
                                // the escape's mistake, not another one.
                                None | Some('\n') => break,
                                Some(_) => {
                                    self.bump();
                                    continue;
                                }
                            }
                        }
                    };
                    self.bump();
                    value.push(decoded);
                }
                Some(c) => {
                    self.bump();
                    let mut buf = [0; 4];
                    value.extend_from_slice(c.encode_utf8(&mut buf).as_bytes());
                }
            }
        }
        self.push(Tok::Str(value), start);
    }

    /// Skips an expression in a string, from its `{` to the `}` that closes
    /// it. Gives whether that `}` is on the line, so the string goes on.
    fn skip_expression(&mut self) -> bool {
        let mut depth = 0;
        while let Some(c) = self.peek() {
            match c {
                '\n' => return false,
                '{' => depth += 1,
                '}' => {
                    depth -= 1;
                    if depth == 0 {
                        self.bump();
                        return true;
                    }
                }
                _ => {}
            }
            self.bump();
        }
        false
    }
}

/// A character as an error shows it.
fn show_char(c: char) -> String {
    if c.is_control() || c.is_whitespace() {
        format!("U+{:04X}", c as u32)
    } else {
        format!("`{c}`")
    }
}
