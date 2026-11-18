//! Errors and warnings found in a program, each at a line and a column, and
//! how they are written out. A warning is written in the form an error is.

use std::fmt;

/// A place in a source file. Lines and columns count from 1, and a column
/// counts characters, not bytes.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct Pos {
    pub line: usize,
    pub col: usize,
}

impl Pos {
    pub const START: Pos = Pos { line: 1, col: 1 };
}

/// An error in a program, or a warning about it, at the place it was
/// found.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Error {
    pub pos: Pos,
    /// What is wrong.
    pub message: String,
    /// What to write instead, when the compiler knows.
    pub fix: Option<String>,
    /// The module the place is in, when it is not in the file that was
    /// compiled: the name of a module of the standard library, or the path of
    /// the file of a module of a project.
    pub module: Option<String>,
}

impl Error {
    pub fn new(pos: Pos, message: impl Into<String>) -> Error {
        Error {
            pos,
            message: message.into(),
            fix: None,
            module: None,
        }
    }

    /// The same error, with advice on what to write instead.
    pub fn with_fix(mut self, fix: impl Into<String>) -> Error {
        self.fix = Some(fix.into());
        self
    }

    /// The same error, at a place in the module of the library with the name.
    pub fn in_module(mut self, module: impl Into<String>) -> Error {
        self.module = Some(module.into());
        self
    }

    /// An error for a part of the language the compiler does not build yet.
    pub fn unsupported(pos: Pos, feature: &str) -> Error {
        Error::new(pos, format!("{feature} is not supported yet"))
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        if let Some(module) = &self.module {
            write!(f, "{module}:")?;
        }
        write!(f, "{}:{}: {}", self.pos.line, self.pos.col, self.message)
    }
}

/// The source text as errors show it and count its columns: each invalid
/// UTF-8 sequence stands as one replacement character.
pub fn text(source: &[u8]) -> String {
    String::from_utf8_lossy(source).into_owned()
}

/// Writes errors in the one form the compiler prints. Each error gives its
/// place, what is wrong, the line of source with a mark under the place, and
/// a `fix:` line when it has advice. The result ends with a line break.
pub fn render(file: &str, source: &[u8], errors: &[Error]) -> String {
    render_with(file, source, errors, &|module| {
        crate::library_text(module).to_string()
    })
}

/// `render`, with the text of the module an error is in given by `module_text`,
/// which is asked for the name or path the error holds.
pub fn render_with(
    file: &str,
    source: &[u8],
    errors: &[Error],
    module_text: &dyn Fn(&str) -> String,
) -> String {
    let text = text(source);
    let lines: Vec<&str> = text.split('\n').collect();
    let file: String = file.chars().map(shown).collect();
    let mut out = String::new();
    for error in errors {
        let Pos { line, col } = error.pos;
        let message: String = error.message.chars().map(shown).collect();
        let (file, lines) = match &error.module {
            Some(module) => (
                module.chars().map(shown).collect::<String>(),
                module_text(module)
                    .split('\n')
                    .map(str::to_string)
                    .collect::<Vec<String>>(),
            ),
            None => (
                file.clone(),
                lines.iter().map(|l| l.to_string()).collect::<Vec<String>>(),
            ),
        };
        out.push_str(&format!("{file}:{line}:{col}: {message}\n"));
        let number = line.to_string();
        let gutter = " ".repeat(number.len());
        let source_line = lines.get(line - 1).map_or("", String::as_str);
        let shown_line: String = source_line.chars().map(shown).collect();
        let indent: String = source_line
            .chars()
            .take(col - 1)
            .map(|c| " ".repeat(width(shown(c))))
            .collect();
        out.push_str(&format!("  {number} | {shown_line}\n"));
        out.push_str(&format!("  {gutter} | {indent}^\n"));
        if let Some(fix) = &error.fix {
            let fix: String = fix.chars().map(shown).collect();
            out.push_str(&format!("  fix: {fix}\n"));
        }
    }
    out
}

/// A character as printed output shows it: a tab becomes a space, and a
/// character that would control the display becomes a replacement
/// character.
fn shown(c: char) -> char {
    match c {
        '\t' => ' ',
        '\u{200e}'
        | '\u{200f}'
        | '\u{2028}'
        | '\u{2029}'
        | '\u{202a}'..='\u{202e}'
        | '\u{2066}'..='\u{2069}' => '\u{fffd}',
        c if c.is_control() => '\u{fffd}',
        c => c,
    }
}

/// How many places a character takes in a terminal, so that the mark sits
/// under its character.
pub fn width(c: char) -> usize {
    match c as u32 {
        0x0300..=0x036f
        | 0x1ab0..=0x1aff
        | 0x1dc0..=0x1dff
        | 0x200b..=0x200d
        | 0x20d0..=0x20ff
        | 0xfe00..=0xfe0f
        | 0xfe20..=0xfe2f => 0,
        0x1100..=0x115f
        | 0x2e80..=0x303e
        | 0x3041..=0x33ff
        | 0x3400..=0x4dbf
        | 0x4e00..=0x9fff
        | 0xa000..=0xa4cf
        | 0xac00..=0xd7a3
        | 0xf900..=0xfaff
        | 0xfe30..=0xfe4f
        | 0xff00..=0xff60
        | 0xffe0..=0xffe6
        | 0x1f300..=0x1f64f
        | 0x1f680..=0x1f6ff
        | 0x1f900..=0x1f9ff
        | 0x20000..=0x3fffd => 2,
        _ => 1,
    }
}
