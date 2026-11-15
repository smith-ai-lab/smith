//! Errors found in a program, each at a line and a column.

use std::fmt;

/// A place in a source file. Lines and columns count from 1, and a column
/// counts characters, not bytes.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Pos {
    pub line: usize,
    pub col: usize,
}

impl Pos {
    pub const START: Pos = Pos { line: 1, col: 1 };
}

/// An error in a program, at the place it was found.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Error {
    pub pos: Pos,
    pub message: String,
}

impl Error {
    pub fn new(pos: Pos, message: impl Into<String>) -> Error {
        Error {
            pos,
            message: message.into(),
        }
    }

    /// An error for a part of the language the compiler does not build yet.
    pub fn unsupported(pos: Pos, feature: &str) -> Error {
        Error::new(pos, format!("{feature} is not supported yet"))
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}:{}: {}", self.pos.line, self.pos.col, self.message)
    }
}
