//! The comment on each struct, field, enum, and case of a file, which
//! `Describe` tells with the item. It is noted only when the file asks for a
//! description, so a program that describes nothing holds none.

use crate::comments;
use crate::parser::File;

/// Whether the file writes the name of the trait `Describe`, or imports the
/// module that defines it.
pub(crate) fn asks(file: &File) -> bool {
    file.idents.iter().any(|(name, _)| name == "Describe")
        || file
            .uses
            .iter()
            .any(|u| u.module.len() == 2 && u.module[0].0 == "std" && u.module[1].0 == "describe")
}

/// Notes the comment on each struct, field, enum, and case of the file that
/// has one, by the item's name and, for a field or a case, the name of what
/// it is part of before its own, as `Note.title`. A field or a case written
/// on the line of what it belongs to has none of its own.
pub(crate) fn add(file: &mut File, source: &str) {
    let mut found: Vec<(String, String)> = Vec::new();
    let mut note = |name: String, pos: crate::diag::Pos, part: bool| {
        if part && !comments::starts_line(source, pos.line, pos.col) {
            return;
        }
        let comment = comments::above(source, pos.line);
        if !comment.is_empty() {
            found.push((name, comment));
        }
    };
    for s in &file.structs {
        note(s.name.clone(), s.pos, false);
        for f in &s.fields {
            note(format!("{}.{}", s.name, f.name), f.pos, true);
        }
    }
    for e in &file.enums {
        note(e.name.clone(), e.pos, false);
        for c in &e.cases {
            note(format!("{}.{}", e.name, c.name), c.pos, true);
        }
    }
    file.abouts.extend(found);
}
