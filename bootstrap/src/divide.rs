//! Divides the prelude's source into its items without lexing it.
//!
//! `build.rs` does this once, when the compiler is built, so that at run time
//! an `impl` is found by the trait and the types it is for, and only the ones
//! a program reaches are lexed and parsed. It has no dependency, and `build.rs`
//! includes this file as a module, so that a test can divide a copy of the
//! source with the code the build divides it with.
//!
//! An item starts at the start of a line with `enum`, `struct`, `trait`, or
//! `impl`, and ends at the first line after it that is `}` alone.

/// An `impl` of the prelude, whole, with the names its header holds.
#[derive(Debug, PartialEq, Eq)]
pub struct Impl<'a> {
    /// The text of the `impl`, from `impl` to its closing `}`.
    pub source: &'a str,
    /// The trait it is for.
    pub name: &'a str,
    /// Every name after the trait's, in the types the `impl` is for and in its
    /// generics, such as `Result`, `T`, and `str`.
    pub types: Vec<&'a str>,
}

/// The traits whose declarations are read only for a program that names one of
/// them or one of their functions, or imports a module that does: the traits of
/// operations on whole numbers that a program calls by name.
pub const NUMERIC: &[&str] = &["Wrapping", "Saturating", "Checked", "Bits", "Float"];

/// A trait of `NUMERIC`.
#[derive(Debug, PartialEq, Eq)]
pub struct Numeric<'a> {
    pub source: &'a str,
    /// The trait's name, and then the names of its functions.
    pub names: Vec<&'a str>,
}

/// The prelude's source divided: the text of every item that is not an `impl`
/// and is read with every program, the traits of `NUMERIC` and the names that
/// call for them, and each `impl`, in the order written.
#[derive(Debug, PartialEq, Eq)]
pub struct Divided<'a> {
    pub declarations: String,
    /// Each trait of `NUMERIC`: its text, and its name and the names of its
    /// functions.
    pub numeric: Vec<Numeric<'a>>,
    pub impls: Vec<Impl<'a>>,
}

/// Divides the source. A line that is not part of an item is ignored.
pub fn divide(source: &str) -> Divided<'_> {
    let mut declarations = String::new();
    let mut numeric: Vec<Numeric> = Vec::new();
    let mut impls = Vec::new();
    let mut start = None;
    let mut at = 0;
    for line in source.split_inclusive('\n') {
        let end = at + line.len();
        if start.is_none() {
            if ["enum ", "struct ", "trait ", "impl"]
                .iter()
                .any(|k| line.starts_with(k))
            {
                start = Some(at);
            } else {
                assert!(line.trim().is_empty(), "text outside an item: {line}");
            }
        }
        if let Some(from) = start
            && line.trim_end() == "}"
        {
            let text = &source[from..end];
            if text.starts_with("impl") {
                impls.push(header(text));
            } else if let Some(name) = text
                .strip_prefix("trait ")
                .map(|rest| rest.split(['[', ' ']).next().unwrap())
                .filter(|name| NUMERIC.contains(name))
            {
                let mut names = vec![name];
                for line in text.lines() {
                    if let Some(rest) = line.trim_start().strip_prefix("fn ") {
                        names.push(rest.split('(').next().unwrap());
                    }
                }
                numeric.push(Numeric {
                    source: text,
                    names,
                });
            } else {
                declarations.push_str(text);
                declarations.push('\n');
            }
            start = None;
        }
        at = end;
    }
    assert!(start.is_none(), "an item of the prelude is not closed");
    Divided {
        declarations,
        numeric,
        impls,
    }
}

/// The names in the first line of an `impl`, as `impl[T: Exit] Exit[Result[T]] {`:
/// the trait is the name before the first `[` after the generics.
fn header(text: &str) -> Impl<'_> {
    let line = text.lines().next().unwrap();
    let rest = line.strip_prefix("impl").unwrap();
    let (generics, rest) = if rest.starts_with('[') {
        let close = matching(rest);
        (&rest[..=close], rest[close + 1..].trim_start())
    } else {
        ("", rest.trim_start())
    };
    let name = rest.split(['[', ' ']).next().unwrap();
    let mut types = names(generics);
    types.extend(names(&rest[name.len()..]));
    Impl {
        source: text,
        name,
        types,
    }
}

/// The index of the `]` that closes the `[` the text starts with.
fn matching(text: &str) -> usize {
    let mut depth = 0;
    for (i, c) in text.char_indices() {
        match c {
            '[' => depth += 1,
            ']' => {
                depth -= 1;
                if depth == 0 {
                    return i;
                }
            }
            _ => {}
        }
    }
    panic!("a `[` is not closed in `{text}`")
}

/// Every name in the text, in order.
fn names(text: &str) -> Vec<&str> {
    text.split(|c: char| !(c.is_alphanumeric() || c == '_'))
        .filter(|n| !n.is_empty())
        .collect()
}
