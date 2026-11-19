//! Finds where each item of a module starts and ends without lexing or
//! parsing it, so that a module is read for the items a program reaches.
//!
//! An item starts at a line whose first character is not a space, not part of
//! a comment, and not a closing bracket, while no bracket is open, and it
//! ends where the next item starts. Brackets are counted outside strings,
//! characters, and comments. A source that does not follow this, or has
//! anything at the top that is not a `use`, a `fn`, a `struct`, an `enum`, a
//! `trait`, or an `impl`, is not divided, and is read whole.

/// What an item is.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    Use,
    Fn,
    Struct,
    Enum,
    Trait,
    Impl,
}

/// An item, as the places in the source it is between.
#[derive(Clone, Copy, Debug)]
pub struct Piece {
    pub kind: Kind,
    /// Whether it is written with `pub`.
    pub public: bool,
    /// The place of its name, which for an `impl` is its trait's.
    pub name: (usize, usize),
    /// The place of its text, from its first character to the start of the
    /// next item.
    pub text: (usize, usize),
    /// The number of lines before it, from 0.
    pub line: usize,
}

/// The items of the source, in order, or `None` when it is not divided.
pub fn outline(source: &str) -> Option<Vec<Piece>> {
    let bytes = source.as_bytes();
    // A line that starts in column 1 is at most as many as the lines.
    let lines = bytes.iter().filter(|&&b| b == b'\n').count() + 1;
    let mut pieces: Vec<Piece> = Vec::with_capacity(lines.min(source.len() / 8 + 1));
    let mut depth: i64 = 0;
    let mut at = 0;
    let mut line = 0;
    while at < bytes.len() {
        let end = bytes[at..]
            .iter()
            .position(|&b| b == b'\n')
            .map_or(bytes.len(), |n| at + n + 1);
        let row = &bytes[at..end];
        let first = row[0];
        let starts = depth == 0
            && !matches!(
                first,
                b' ' | b'\t' | b'\n' | b'/' | b'{' | b'}' | b')' | b']'
            );
        if starts {
            if let Some(last) = pieces.last_mut() {
                last.text.1 = at;
            }
            pieces.push(head(source, at, end, line)?);
        } else if pieces.is_empty() && !matches!(first, b'\n' | b'/') {
            // Something before the first item.
            return None;
        }
        depth += brackets(row)?;
        if depth < 0 {
            return None;
        }
        at = end;
        line += 1;
    }
    if depth != 0 {
        return None;
    }
    if let Some(last) = pieces.last_mut() {
        last.text.1 = bytes.len();
    }
    Some(pieces)
}

/// The item whose first line is `source[at..end]`.
fn head(source: &str, at: usize, end: usize, line: usize) -> Option<Piece> {
    let row = &source.as_bytes()[at..end];
    let mut i = 0;
    let word = |i: &mut usize| -> (usize, usize) {
        while *i < row.len() && row[*i] == b' ' {
            *i += 1;
        }
        let from = *i;
        while *i < row.len() && (row[*i].is_ascii_alphanumeric() || row[*i] == b'_') {
            *i += 1;
        }
        (from, *i)
    };
    let text = |(a, b): (usize, usize)| &source[at + a..at + b];
    let mut w = word(&mut i);
    let public = text(w) == "pub";
    if public {
        w = word(&mut i);
    }
    let kind = match text(w) {
        "use" => Kind::Use,
        "fn" => Kind::Fn,
        "struct" => Kind::Struct,
        "enum" => Kind::Enum,
        "trait" => Kind::Trait,
        "impl" => Kind::Impl,
        _ => return None,
    };
    if kind == Kind::Impl {
        // Generics, then the trait.
        while i < row.len() && row[i] == b' ' {
            i += 1;
        }
        if row.get(i) == Some(&b'[') {
            let mut depth = 0;
            while i < row.len() {
                match row[i] {
                    b'[' => depth += 1,
                    b']' => {
                        depth -= 1;
                        if depth == 0 {
                            i += 1;
                            break;
                        }
                    }
                    _ => {}
                }
                i += 1;
            }
            if depth != 0 {
                return None;
            }
        }
    }
    let name = if kind == Kind::Use {
        (0, 0)
    } else {
        word(&mut i)
    };
    if name.0 == name.1 && kind != Kind::Use {
        return None;
    }
    Some(Piece {
        kind,
        public,
        name: (at + name.0, at + name.1),
        text: (at, end),
        line,
    })
}

/// How much more brackets a line opens than it closes, outside strings,
/// characters, and a comment, or `None` when a string or a character is not
/// closed on the line.
fn brackets(row: &[u8]) -> Option<i64> {
    let mut depth = 0;
    let mut i = 0;
    while i < row.len() {
        match row[i] {
            b'/' if row.get(i + 1) == Some(&b'/') => break,
            b'"' => i = string(row, i + 1)?,
            b'\'' => {
                i += 1;
                if row.get(i) == Some(&b'\\') {
                    i += 1;
                }
                // One character, which may be of several bytes.
                i += 1;
                while row.get(i).is_some_and(|b| b & 0xC0 == 0x80) {
                    i += 1;
                }
                if row.get(i) != Some(&b'\'') {
                    return None;
                }
            }
            b'{' | b'(' | b'[' => depth += 1,
            b'}' | b')' | b']' => depth -= 1,
            _ => {}
        }
        i += 1;
    }
    Some(depth)
}

/// The place of the quote that closes the string that `row[from..]` is in
/// the text of, which may hold expressions in braces that hold strings.
fn string(row: &[u8], from: usize) -> Option<usize> {
    let mut i = from;
    while i < row.len() {
        match row[i] {
            b'\\' => i += 1,
            b'"' => return Some(i),
            b'{' => {
                let mut depth = 1;
                i += 1;
                while depth > 0 {
                    match *row.get(i)? {
                        b'"' => i = string(row, i + 1)?,
                        b'{' => depth += 1,
                        b'}' => depth -= 1,
                        _ => {}
                    }
                    i += 1;
                }
                continue;
            }
            _ => {}
        }
        i += 1;
    }
    None
}
