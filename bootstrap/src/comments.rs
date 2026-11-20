//! The comment on an item: the lines of `//` directly above the line the item
//! starts on. Nothing is kept when a file is read. A comment is found from the
//! source where it is asked for, by the line of the item it describes, so a
//! program that asks for none pays for none.

/// The comment on the item that starts on `line`, counted from 1: the run of
/// lines that start with `//`, after any spaces, which ends on the line above
/// it, with the `//` and one space after it taken off each, and the lines
/// joined by a line break. A blank line, or a line that is not a comment,
/// ends the run, so a comment with a blank line after it is no one's. An item
/// with no comment above it has the empty text.
pub(crate) fn above(source: &str, line: usize) -> String {
    // The start of each line from the line above the item up, until a line
    // that is not a comment.
    let mut rows: Vec<&str> = Vec::new();
    let mut end = start_of(source, line);
    while end > 0 && rows.len() < line {
        let before = &source[..end - 1];
        let begin = before.rfind('\n').map_or(0, |n| n + 1);
        let row = &before[begin..];
        let Some(rest) = row.trim_start_matches([' ', '\t']).strip_prefix("//") else {
            break;
        };
        rows.push(rest.strip_prefix(' ').unwrap_or(rest));
        end = begin;
    }
    rows.reverse();
    rows.join("\n")
}

/// Where line `line`, counted from 1, starts in the source, or the length of
/// the source when it has fewer lines.
fn start_of(source: &str, line: usize) -> usize {
    let mut at = 0;
    for _ in 1..line {
        match source[at..].find('\n') {
            Some(n) => at += n + 1,
            None => return source.len(),
        }
    }
    at
}

/// Whether the item whose name is at the column `col` of line `line`, counted
/// from 1, starts its line: nothing but spaces comes before it. A parameter, a
/// field, or a case written on the line of what it belongs to has no comment
/// of its own, and the comment above that line is the whole thing's.
pub(crate) fn starts_line(source: &str, line: usize, col: usize) -> bool {
    let start = start_of(source, line);
    let row = source[start..].split('\n').next().unwrap_or("");
    // A column is counted in characters.
    row.chars()
        .take(col.saturating_sub(1))
        .all(|c| c == ' ' || c == '\t')
}
