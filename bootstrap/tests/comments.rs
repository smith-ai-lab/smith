//! The comment on an item is the run of `//` lines directly above the line it
//! starts on, with the `//` and one space taken off each, kept as lines. A
//! blank line between the run and the item leaves the item with none, and so
//! does a line of code. The expectation is what the generator that wrote the
//! file says each item was given.

use smith::item_comments;

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "the_comment_on_each_item_is_the_run_of_comment_lines_directly_above_it",
        the_comment_on_each_item_is_the_run_of_comment_lines_directly_above_it,
    ),
    (
        "a_comment_with_a_blank_line_or_code_between_it_and_an_item_is_not_the_items",
        a_comment_with_a_blank_line_or_code_between_it_and_an_item_is_not_the_items,
    ),
    (
        "a_part_that_shares_its_line_with_what_it_belongs_to_has_no_comment_of_its_own",
        a_part_that_shares_its_line_with_what_it_belongs_to_has_no_comment_of_its_own,
    ),
    (
        "a_file_with_no_comment_has_no_comment_on_any_item",
        a_file_with_no_comment_has_no_comment_on_any_item,
    ),
];

/// A comment as the author wrote it: the lines of the text after `//`, and the
/// text the item has from it.
#[derive(Clone, Debug)]
struct Comment {
    /// What follows `//` on each line.
    lines: Vec<String>,
}

impl Comment {
    fn written(&self, indent: &str) -> String {
        (self.lines.iter())
            .map(|l| format!("{indent}//{l}\n"))
            .collect()
    }

    /// One space after `//` is taken off, and nothing else.
    fn text(&self) -> String {
        let lines: Vec<&str> = (self.lines.iter())
            .map(|l| l.strip_prefix(' ').unwrap_or(l))
            .collect();
        lines.join("\n")
    }
}

/// What stands between a comment and its item.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Between {
    Nothing,
    Blank,
}

/// A thing that may have a comment: whether it has one, and what is between.
#[derive(Clone, Debug)]
struct Slot {
    comment: Option<Comment>,
    between: Between,
}

impl Slot {
    /// The comment the item has, if the comment is directly above it. A
    /// comment that says nothing is the empty text, as no comment is.
    fn has(&self) -> Option<String> {
        match (&self.comment, self.between) {
            (Some(c), Between::Nothing) => Some(c.text()).filter(|t| !t.is_empty()),
            _ => None,
        }
    }

    fn written(&self, indent: &str) -> String {
        let mut text = String::new();
        if let Some(c) = &self.comment {
            text += &c.written(indent);
            if self.between == Between::Blank {
                text += "\n";
            }
        }
        text
    }
}

#[derive(Clone, Debug)]
enum Item {
    Function(Slot, Vec<Slot>),
    Struct(Slot, Vec<Slot>),
    Enum(Slot, Vec<Slot>),
}

fn line(g: &mut Gen) -> String {
    let words = ["the length", "How many", "x", "a  b", "", "An item."];
    let lead = ["", " ", "  "];
    format!("{}{}", g.pick(&lead), g.pick(&words))
}

fn slot(g: &mut Gen) -> Slot {
    let comment = (g.below(3) != 0).then(|| Comment {
        lines: (0..g.below(3) + 1).map(|_| line(g)).collect(),
    });
    Slot {
        comment,
        between: if g.below(4) == 0 {
            Between::Blank
        } else {
            Between::Nothing
        },
    }
}

fn items(g: &mut Gen) -> Vec<Item> {
    (0..g.below(4) + 1)
        .map(|_| {
            let parts: Vec<Slot> = (0..g.below(3) + 1).map(|_| slot(g)).collect();
            match g.below(3) {
                0 => Item::Function(slot(g), parts),
                1 => Item::Struct(slot(g), parts),
                _ => Item::Enum(slot(g), parts),
            }
        })
        .collect()
}

/// The file the items are, and each comment the items should have, by name.
fn file(items: &[Item]) -> (String, Vec<(String, String)>) {
    let mut text = String::new();
    let mut said = Vec::new();
    for (k, item) in items.iter().enumerate() {
        let (head, parts) = match item {
            Item::Function(s, p) | Item::Struct(s, p) | Item::Enum(s, p) => (s, p),
        };
        let name = match item {
            Item::Function(..) => format!("f{k}"),
            Item::Struct(..) => format!("S{k}"),
            Item::Enum(..) => format!("E{k}"),
        };
        text += &head.written("");
        if let Some(c) = head.has() {
            said.push((name.clone(), c));
        }
        let part_name = |i: usize| match item {
            Item::Function(..) => format!("a{i}"),
            Item::Struct(..) => format!("v{i}"),
            Item::Enum(..) => format!("c{i}"),
        };
        match item {
            Item::Function(..) => text += &format!("fn {name}(\n"),
            Item::Struct(..) => text += &format!("struct {name} {{\n"),
            Item::Enum(..) => text += &format!("enum {name} {{\n"),
        }
        for (i, part) in parts.iter().enumerate() {
            text += &part.written("  ");
            if let Some(c) = part.has() {
                said.push((format!("{name}.{}", part_name(i)), c));
            }
            text += &match item {
                Item::Function(..) => format!("  {}: str,\n", part_name(i)),
                Item::Struct(..) => format!("  {}: str\n", part_name(i)),
                Item::Enum(..) => format!("  {}\n", part_name(i)),
            };
        }
        text += match item {
            Item::Function(..) => ") -> str { a0 }\n\n",
            _ => "}\n\n",
        };
    }
    (text, said)
}

/// A function, a struct, an enum, and each parameter, field, and case of them,
/// has the comment above it when it has one directly above, whatever the lines
/// of the comment hold.
fn the_comment_on_each_item_is_the_run_of_comment_lines_directly_above_it() {
    generate::check(7901, 60, items, |items| {
        let (source, said) = file(items);
        let found = item_comments(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
        assert_eq!(found, said, "for\n{source}");
    });
}

/// The comment of the page, and one that a blank line or a line of code
/// separates from what follows.
fn a_comment_with_a_blank_line_or_code_between_it_and_an_item_is_not_the_items() {
    let source = "// Makes a length longer or shorter.\n// Twice, by default.\n\
        fn scale(\n  // The length.\n  x: str,\n  by: str,\n) -> str { x }\n\n\
        // Not scale's.\n\nfn other() -> str { \"x\" }\n\n\
        // Of the first line only.\nfn third() -> str { \"y\" }\n";
    let found = item_comments(source.as_bytes()).expect("a file");
    assert_eq!(
        found,
        vec![
            (
                "scale".to_string(),
                "Makes a length longer or shorter.\nTwice, by default.".to_string()
            ),
            ("scale.x".to_string(), "The length.".to_string()),
            ("third".to_string(), "Of the first line only.".to_string()),
        ]
    );
    // After code, a comment on the same line is refused as it was.
    let refused = item_comments(b"fn f() -> str { \"x\" } // no\n");
    assert!(refused.is_err());
}

/// A file with no `//` has no comment on anything.
fn a_file_with_no_comment_has_no_comment_on_any_item() {
    generate::check(7902, 20, items, |items| {
        let (source, _) = file(items);
        let bare: String = (source.lines())
            .filter(|l| !l.trim_start().starts_with("//"))
            .map(|l| format!("{l}\n"))
            .collect();
        assert_eq!(item_comments(bare.as_bytes()), Ok(Vec::new()), "for\n{bare}");
    });
}

/// A parameter written on the line of the function has no comment of its own,
/// and neither has one written on the line of another parameter: the comment
/// above is the thing's that starts the line.
fn a_part_that_shares_its_line_with_what_it_belongs_to_has_no_comment_of_its_own() {
    let source = "// Adds two.\nfn add(a: str, b: str) -> str { a }\n\n\
        // Adds.\nfn plus(\n  // The first.\n  a: str, b: str,\n) -> str { a }\n\n\
        // A pair.\nstruct Pair { a: str }\n";
    let found = item_comments(source.as_bytes()).expect("a file");
    assert_eq!(
        found,
        vec![
            ("add".to_string(), "Adds two.".to_string()),
            ("plus".to_string(), "Adds.".to_string()),
            ("plus.a".to_string(), "The first.".to_string()),
            ("Pair".to_string(), "A pair.".to_string()),
        ]
    );
}
