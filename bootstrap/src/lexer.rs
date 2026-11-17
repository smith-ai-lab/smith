//! Turns source text into tokens, following the lexical rules in
//! `knowledge/grammar.md`.

use crate::diag::{Error, Pos};

/// The error for a string whose line ends before its closing quote.
const NO_CLOSING_QUOTE: &str = "this string has no closing quote on the line it starts";

/// The keywords of Smith.
const KEYWORDS: &[&str] = &[
    "and", "as", "break", "derive", "effect", "else", "ensures", "enum", "fn", "for", "foreign",
    "if", "impl", "in", "loop", "match", "not", "or", "out", "pub", "requires", "struct", "take",
    "trait", "try", "use", "var", "with", "without",
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
    /// A string literal that holds expressions, as its pieces in order.
    Interp(Vec<Piece>),
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
            Tok::Str(_) | Tok::Interp(_) => "a string".to_string(),
            Tok::Punct(p) => format!("`{p}`"),
            Tok::Newline => "a line break".to_string(),
            Tok::Eof => "the end of the file".to_string(),
        }
    }
}

/// A piece of a string literal that holds expressions.
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Piece {
    /// Text, with its escapes applied.
    Text(Vec<u8>),
    /// The tokens of an expression in braces, followed by the `}` that
    /// closes it.
    Expr(Vec<Token>),
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
                '\n' => {
                    self.bump();
                    self.push(Tok::Newline, start);
                }
                _ => self.token(start, c),
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

    /// Reads what starts with `c`, which is not a line break: a space, a
    /// comment, or a token.
    fn token(&mut self, start: Pos, c: char) {
        match c {
            ' ' | '\t' => {
                self.bump();
            }
            '/' if self.peek_second() == Some('/') => self.comment(start),
            '"' => self.string(start),
            '\'' => self.character(start),
            'a'..='z' | 'A'..='Z' | '_' => self.word(start),
            '0'..='9' => self.number(start),
            _ => self.punctuation(start, c),
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
        // The sign of an exponent belongs to the number.
        let so_far = &before[..before.len() - self.rest.len()];
        if so_far.ends_with(['e', 'E'])
            && !so_far.starts_with("0x")
            && matches!(self.peek(), Some('+' | '-'))
            && self.peek_second().is_some_and(|c| c.is_ascii_digit())
        {
            self.bump();
            self.take_while(word);
        }
        let text = &before[..before.len() - self.rest.len()];
        if let Err(why) = check_number(text) {
            self.error(Error::new(
                start,
                format!("`{text}` is not a number: {why}"),
            ));
        }
        self.push(Tok::Number(text.to_string()), start);
    }

    /// Reads a character between single quotes, which is a whole number
    /// and is kept in its source spelling, as a number is.
    fn character(&mut self, start: Pos) {
        self.bump();
        let mut text = String::from("'");
        let at = self.pos;
        let ok = match self.peek() {
            None | Some('\n') => {
                self.error(Error::new(start, "a character has no closing `'`"));
                return;
            }
            Some('\'') => {
                self.bump();
                self.error(
                    Error::new(
                        start,
                        "a character is written between single quotes, and this has none",
                    )
                    .with_fix("write `'\\''` for a single quote"),
                );
                return;
            }
            Some('\\') => {
                self.bump();
                match self.peek() {
                    Some(c @ ('\'' | '\\' | 'n' | 't')) => {
                        self.bump();
                        text.push('\\');
                        text.push(c);
                        true
                    }
                    other => {
                        let after = match other {
                            None | Some('\n') => "nothing".to_string(),
                            Some(c) => show_char(c),
                        };
                        self.error(Error::new(
                            at,
                            format!(
                                "unknown escape: a backslash is followed by {after}, \
                                 not one of ' \\ n t"
                            ),
                        ));
                        if matches!(other, Some(c) if c != '\n') {
                            self.bump();
                        }
                        false
                    }
                }
            }
            Some(c) => {
                self.bump();
                text.push(c);
                true
            }
        };
        if self.peek() == Some('\'') {
            self.bump();
            text.push('\'');
            if ok {
                self.push(Tok::Number(text), start);
            }
        } else {
            self.error(Error::new(
                start,
                "a character has no closing `'`, and holds one character only",
            ));
        }
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
        let mut pieces = Vec::new();
        let mut value = Vec::new();
        loop {
            let at = self.pos;
            match self.peek() {
                None | Some('\n') => {
                    self.error(Error::new(start, NO_CLOSING_QUOTE));
                    break;
                }
                Some('"') => {
                    self.bump();
                    break;
                }
                Some('{') => {
                    self.bump();
                    pieces.push(Piece::Text(std::mem::take(&mut value)));
                    match self.expression(start) {
                        Some(tokens) => pieces.push(Piece::Expr(tokens)),
                        None => break,
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
        if pieces.is_empty() {
            self.push(Tok::Str(value), start);
        } else {
            pieces.push(Piece::Text(value));
            pieces.retain(|p| *p != Piece::Text(Vec::new()));
            self.push(Tok::Interp(pieces), start);
        }
    }

    /// Reads the tokens of an expression in a string, the string that
    /// starts at `start`, from after its `{` through the `}` that closes it.
    /// Gives `None` when the line ends first, so the string has no closing
    /// quote, which is reported.
    fn expression(&mut self, start: Pos) -> Option<Vec<Token>> {
        let outside = std::mem::take(&mut self.tokens);
        let mut depth = 0usize;
        let closed = loop {
            let at = self.pos;
            match self.peek() {
                None | Some('\n') => break false,
                Some('}') if depth == 0 => {
                    self.bump();
                    self.push(Tok::Punct("}"), at);
                    break true;
                }
                Some(c) => {
                    match c {
                        '{' => depth += 1,
                        '}' => depth -= 1,
                        _ => {}
                    }
                    self.token(at, c);
                }
            }
        };
        let tokens = std::mem::replace(&mut self.tokens, outside);
        // A string in the braces that the line ends inside is the one
        // without its quote.
        let inner = tokens
            .iter()
            .any(|t| matches!(t.tok, Tok::Str(_) | Tok::Interp(_)))
            && self
                .errors
                .last()
                .is_some_and(|e| e.message == NO_CLOSING_QUOTE);
        if !closed && !inner {
            self.error(Error::new(start, NO_CLOSING_QUOTE));
        }
        closed.then_some(tokens)
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

/// Whether a run of digits of a base, which `valid` says, is written as the
/// lexical rules say: at least one digit, and an `_` only between two
/// digits.
fn digits(text: &str, valid: impl Fn(char) -> bool, base: &str) -> Result<(), String> {
    if text.is_empty() {
        return Err(format!("{base} digits are needed here"));
    }
    let mut previous = None;
    for c in text.chars() {
        if c == '_' {
            if previous.is_none_or(|p| p == '_') {
                return Err("an `_` stands only between two digits".to_string());
            }
        } else if !valid(c) {
            return Err(format!("{} is not a {base} digit", show_char(c)));
        }
        previous = Some(c);
    }
    if previous == Some('_') {
        return Err("an `_` stands only between two digits".to_string());
    }
    Ok(())
}

/// Whether the text of a number, as the lexer reads it, is written as the
/// lexical rules say, and why not if it is not.
fn check_number(text: &str) -> Result<(), String> {
    if let Some(rest) = text.strip_prefix("0x") {
        return digits(rest, |c| c.is_ascii_hexdigit(), "hexadecimal");
    }
    if let Some(rest) = text.strip_prefix("0b") {
        return digits(rest, |c| c == '0' || c == '1', "binary");
    }
    let decimal = |c: char| c.is_ascii_digit();
    let (mantissa, exponent) = match text.find(['e', 'E']) {
        Some(at) => (&text[..at], Some(&text[at + 1..])),
        None => (text, None),
    };
    let (whole, fraction) = match mantissa.split_once('.') {
        Some((whole, fraction)) => (whole, Some(fraction)),
        None => (mantissa, None),
    };
    digits(whole, decimal, "decimal")?;
    if let Some(fraction) = fraction {
        digits(fraction, decimal, "decimal")?;
    }
    if let Some(exponent) = exponent {
        digits(exponent.trim_start_matches(['+', '-']), decimal, "decimal")?;
    }
    Ok(())
}
