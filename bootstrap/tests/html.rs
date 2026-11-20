//! `std.html` writes a page, and every text in it is written so that it is
//! text and nothing else.
//!
//! The generator builds the texts and the reference that says what each must
//! become is written here, so the tests share nothing with the library.

use smith::target::Target;
use smith::{compile, lower, scalar};

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "the_five_bytes_of_the_page_are_written_as_entities_and_nothing_else_is",
        the_five_bytes_of_the_page_are_written_as_entities_and_nothing_else_is,
    ),
    (
        "a_text_is_escaped_as_the_reference_escapes_it_part_one",
        a_text_is_escaped_as_the_reference_escapes_it_part_one,
    ),
    (
        "a_text_is_escaped_as_the_reference_escapes_it_part_two",
        a_text_is_escaped_as_the_reference_escapes_it_part_two,
    ),
    (
        "an_escaped_text_holds_no_byte_of_markup_that_the_text_did_not_have_as_an_entity",
        an_escaped_text_holds_no_byte_of_markup_that_the_text_did_not_have_as_an_entity,
    ),
    (
        "text_writes_a_text_as_the_reference_escapes_it",
        text_writes_a_text_as_the_reference_escapes_it,
    ),
    (
        "what_text_writes_holds_no_byte_of_markup_that_the_text_did_not_have_as_an_entity",
        what_text_writes_holds_no_byte_of_markup_that_the_text_did_not_have_as_an_entity,
    ),
    (
        "the_examples_of_the_page_give_the_pages_the_page_says",
        the_examples_of_the_page_give_the_pages_the_page_says,
    ),
    (
        "a_text_given_to_an_element_or_an_attribute_is_escaped_as_the_reference_escapes_it",
        a_text_given_to_an_element_or_an_attribute_is_escaped_as_the_reference_escapes_it,
    ),
    (
        "an_operation_of_markup_is_refused_outside_std_html_to_import_and_markup_is_refused_to_handle",
        an_operation_of_markup_is_refused_outside_std_html_to_import_and_markup_is_refused_to_handle,
    ),
    (
        "the_examples_of_the_page_build_for_every_target",
        the_examples_of_the_page_build_for_every_target,
    ),
];

/// What the page says a text becomes: `<`, `>`, `&`, `"`, and `'` are
/// written as the entities HTML writes them as, and every other character as
/// it is.
fn reference_escape(text: &str) -> String {
    let mut written = String::new();
    for c in text.chars() {
        match c {
            '<' => written += "&lt;",
            '>' => written += "&gt;",
            '&' => written += "&amp;",
            '"' => written += "&quot;",
            '\'' => written += "&#39;",
            c => written.push(c),
        }
    }
    written
}

/// The text a text of the reference becomes when each entity is read as the
/// character it stands for, or none when a `&` begins no entity.
fn reference_unescape(written: &str) -> Option<String> {
    let mut text = String::new();
    let mut rest = written;
    while let Some(c) = rest.chars().next() {
        if c == '&' {
            let (entity, stands_for) = [
                ("&lt;", '<'),
                ("&gt;", '>'),
                ("&amp;", '&'),
                ("&quot;", '"'),
                ("&#39;", '\''),
            ]
            .into_iter()
            .find(|(entity, _)| rest.starts_with(entity))?;
            text.push(stands_for);
            rest = &rest[entity.len()..];
        } else {
            text.push(c);
            rest = &rest[c.len_utf8()..];
        }
    }
    Some(text)
}

/// The source of a Smith text that is `text`.
fn literal(text: &str) -> String {
    let mut written = String::from("\"");
    for c in text.chars() {
        match c {
            '"' => written += "\\\"",
            '\\' => written += "\\\\",
            '{' => written += "\\{",
            '}' => written += "\\}",
            c => written.push(c),
        }
    }
    written + "\""
}

/// The lines a program that prints `escape` of each text prints.
fn escaped(texts: &[String]) -> Vec<String> {
    let mut body = String::new();
    for text in texts {
        body += &format!("  print(escape({}))\n", literal(text));
    }
    let source = format!(
        "use std.console (console, print)\nuse std.html (escape)\n\nfn main() ! console, alloc {{\n{body}}}\n"
    );
    let lowered = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let outcome = scalar::run(&lowered);
    assert_eq!(outcome.status, 0, "for {source}");
    let printed = String::from_utf8(outcome.stdout).unwrap();
    let lines: Vec<String> = printed.lines().map(str::to_string).collect();
    assert_eq!(lines.len(), texts.len(), "for {source}");
    lines
}

/// A text of the five bytes that are escaped, of pieces that read as an
/// entity already, of characters of one to four bytes, and of plain ones.
fn any_text(g: &mut Gen) -> String {
    const PIECES: &[&str] = &[
        "<", ">", "&", "\"", "'", "&lt;", "&amp;", "&#39;", "a", "z", " ", "é", "日", "😀", "0",
        "/", "=",
    ];
    (0..g.below(12)).map(|_| *g.pick(PIECES)).collect()
}

fn the_five_bytes_of_the_page_are_written_as_entities_and_nothing_else_is() {
    let texts: Vec<String> = ["<", ">", "&", "\"", "'", "", "plain", "a<b>c", "é<日>😀"]
        .map(String::from)
        .to_vec();
    assert_eq!(
        escaped(&texts),
        ["&lt;", "&gt;", "&amp;", "&quot;", "&#39;", "", "plain", "a&lt;b&gt;c", "é&lt;日&gt;😀"]
    );
}

fn a_text_is_escaped_as_the_reference_escapes_it(part: u64) {
    generate::check_part(
        8301,
        40,
        part,
        2,
        |g| (0..10).map(|_| any_text(g)).collect::<Vec<_>>(),
        |texts| {
            let expected: Vec<String> = texts.iter().map(|t| reference_escape(t)).collect();
            assert_eq!(escaped(texts), expected);
        },
    );
}

fn a_text_is_escaped_as_the_reference_escapes_it_part_one() {
    a_text_is_escaped_as_the_reference_escapes_it(0);
}

fn a_text_is_escaped_as_the_reference_escapes_it_part_two() {
    a_text_is_escaped_as_the_reference_escapes_it(1);
}

fn an_escaped_text_holds_no_byte_of_markup_that_the_text_did_not_have_as_an_entity() {
    generate::check(
        8302,
        8,
        |g| (0..10).map(|_| any_text(g)).collect::<Vec<_>>(),
        |texts| {
            for (text, written) in texts.iter().zip(escaped(texts)) {
                assert!(
                    !written.contains(['<', '>', '"', '\'']),
                    "{written:?} holds a byte of markup"
                );
                // Every `&` begins an entity, and reading them back gives the text.
                assert_eq!(reference_unescape(&written).as_deref(), Some(text.as_str()));
            }
        },
    );
}

fn text_writes_a_text_as_the_reference_escapes_it() {
    generate::check(
        8303,
        6,
        |g| (0..8).map(|_| any_text(g)).collect::<Vec<_>>(),
        |texts| {
            let calls: Vec<String> = texts.iter().map(|t| format!("text({})", literal(t))).collect();
            let expected: Vec<String> = texts.iter().map(|t| reference_escape(t)).collect();
            assert_eq!(shown("", &calls), expected);
        },
    );
}

fn what_text_writes_holds_no_byte_of_markup_that_the_text_did_not_have_as_an_entity() {
    generate::check(
        8304,
        6,
        |g| (0..8).map(|_| any_text(g)).collect::<Vec<_>>(),
        |texts| {
            let calls: Vec<String> = texts.iter().map(|t| format!("text({})", literal(t))).collect();
            for (text, line) in texts.iter().zip(shown("", &calls)) {
                assert!(!line.contains(['<', '>', '"', '\'']), "{line:?} holds a byte of markup");
                assert_eq!(reference_unescape(&line).as_deref(), Some(text.as_str()));
            }
        },
    );
}

/// The elements and the names that the programs below use.
const IMPORTS: &str = "use std.console (console, print)
use std.html (Html, a, article, b, body, br, h1, h2, head, html, input, insert, li, markup, p, render, span, text, title, ul)
";

/// The lines a program prints that has the functions `functions` and, for each
/// of `calls`, renders the call as a page and prints it, as a page is read
/// through `Fmt`.
fn shown(functions: &str, calls: &[String]) -> Vec<String> {
    let mut body = String::new();
    for (i, call) in calls.iter().enumerate() {
        body += &format!("  page{i} = render() {{\n    {call}\n  }}\n  print(\"{{page{i}}}\")\n");
    }
    let source = format!("{IMPORTS}\n{functions}\nfn main() ! console, alloc {{\n{body}}}\n");
    let lowered = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let outcome = scalar::run(&lowered);
    assert_eq!(outcome.status, 0, "for {source}");
    let printed = String::from_utf8(outcome.stdout).unwrap();
    let lines: Vec<String> = printed.lines().map(str::to_string).collect();
    assert_eq!(lines.len(), calls.len(), "for {source}");
    lines
}

/// The functions of the examples of the page, each a component, and a call
/// of each with what the page says it writes.
const EXAMPLES: &str = "struct Note {
  title: str
  body: str
  pinned: bool
}

fn hello(name: str) ! markup, alloc {
  html() {
    head() {
      title(\"Hello\")
    }
    body() {
      h1(\"Hello, {name}\")
      p(\"This page is written in Smith.\")
    }
  }
}

fn mixed(name: str) ! markup, alloc {
  p() {
    text(\"Hello, \")
    b(name)
    text(\"!\")
  }
}

fn links() ! markup, alloc {
  a(\"Open\", href: \"/notes/7\", class: \"button\")
  input(name: \"title\", placeholder: \"Title\")
  br()
}

fn note_card(note: Note) ! markup, alloc {
  article(class: \"card\") {
    h2(note.title)
    p(note.body)
    if note.pinned {
      span(\"Pinned\", class: \"badge\")
    }
  }
}

fn notes(all: List[Note]) ! markup, alloc {
  ul() {
    loop note in all {
      li() {
        note_card(note)
      }
    }
  }
}

fn layout(heading: str, inside: fn() ! markup) ! markup, alloc {
  html() {
    head() {
      title(heading)
    }
    body() {
      h1(heading)
      inside()
    }
  }
}

fn about() ! markup, alloc {
  layout(\"About\") {
    p(\"Smith is a programming language.\")
  }
}

fn frame(inside: Html) ! markup, alloc {
  span(class: \"frame\") {
    insert(inside)
  }
}

fn framed() ! markup, alloc {
  inside = render() {
    about()
  }
  frame(inside)
}
";

fn the_examples_of_the_page_give_the_pages_the_page_says() {
    let calls: Vec<String> = [
        "hello(\"Ana\")",
        "mixed(\"Ana\")",
        "links()",
        "notes([Note(title: \"a\", body: \"x\", pinned: true), Note(title: \"b\", body: \"y\", pinned: false)])",
        "about()",
        "framed()",
    ]
    .map(String::from)
    .to_vec();
    assert_eq!(
        shown(EXAMPLES, &calls),
        [
            "<html><head><title>Hello</title></head><body><h1>Hello, Ana</h1><p>This page is written in Smith.</p></body></html>",
            "<p>Hello, <b>Ana</b>!</p>",
            "<a href=\"/notes/7\" class=\"button\">Open</a><input name=\"title\" placeholder=\"Title\"><br>",
            "<ul><li><article class=\"card\"><h2>a</h2><p>x</p><span class=\"badge\">Pinned</span></article></li><li><article class=\"card\"><h2>b</h2><p>y</p></article></li></ul>",
            "<html><head><title>About</title></head><body><h1>About</h1><p>Smith is a programming language.</p></body></html>",
            "<span class=\"frame\"><html><head><title>About</title></head><body><h1>About</h1><p>Smith is a programming language.</p></body></html></span>",
        ]
    );
}

fn a_text_given_to_an_element_or_an_attribute_is_escaped_as_the_reference_escapes_it() {
    generate::check(
        8305,
        6,
        |g| (0..6).map(|_| any_text(g)).collect::<Vec<_>>(),
        |texts| {
            let mut calls = Vec::new();
            let mut expected = Vec::new();
            for t in texts {
                let (lit, e) = (literal(t), reference_escape(t));
                calls.push(format!("h1({lit})"));
                expected.push(format!("<h1>{e}</h1>"));
                // A text that is empty is what a parameter has by default, so it writes nothing.
                let attribute = |name: &str| if t.is_empty() { String::new() } else { format!(" {name}=\"{e}\"") };
                calls.push(format!("p(\"x\", class: {lit})"));
                expected.push(format!("<p{}>x</p>", attribute("class")));
                calls.push(format!("a({lit}, href: {lit}, id: {lit})"));
                expected.push(format!("<a{}{}>{e}</a>", attribute("href"), attribute("id")));
            }
            // An attribute that is the empty text is not written.
            calls.push("p(\"x\", class: \"\")".to_string());
            expected.push("<p>x</p>".to_string());
            assert_eq!(shown("", &calls), expected);
        },
    );
}

/// The examples of the page, written with `render` as the page writes them,
/// and `main` reaching each, so that every target builds them.
fn the_examples_of_the_page_build_for_every_target() {
    let source = "use std.html (Html, a, article, b, body, br, h1, h2, head, html, input, insert, li, markup, p, render, span, text, title, ul)

struct Note {
  title: str
  body: str
  pinned: bool
}

fn hello(name: str) -> Html ! alloc {
  render() {
    html() {
      head() {
        title(\"Hello\")
      }
      body() {
        h1(\"Hello, {name}\")
        p(\"This page is written in Smith.\")
      }
    }
  }
}

fn links() -> Html ! alloc {
  render() {
    p() {
      text(\"Hello, \")
      b(\"Ana\")
    }
    a(\"Open\", href: \"/notes/7\", class: \"button\")
    input(name: \"title\", placeholder: \"Title\")
    br()
  }
}

fn note_card(note: Note) ! markup, alloc {
  article(class: \"card\") {
    h2(note.title)
    p(note.body)
    if note.pinned {
      span(\"Pinned\", class: \"badge\")
    }
  }
}

fn notes(all: List[Note]) -> Html ! alloc {
  render() {
    ul() {
      loop note in all {
        li() {
          note_card(note)
        }
      }
    }
  }
}

fn layout(heading: str, inside: fn() ! markup) -> Html ! alloc {
  render() {
    html() {
      head() {
        title(heading)
      }
      body() {
        h1(heading)
        inside()
      }
    }
  }
}

fn about() -> Html ! alloc {
  layout(\"About\") {
    p(\"Smith is a programming language.\")
  }
}

fn frame(inside: Html) -> Html ! alloc {
  render() {
    span(class: \"frame\") {
      insert(inside)
    }
  }
}

fn main() ! alloc {
  one = hello(\"Ana\")
  two = links()
  three = notes([])
  four = frame(about())
}
";
    for target in Target::ALL {
        if let Err(e) = compile(source.as_bytes(), *target) {
            panic!("{e:?} on {target:?}");
        }
    }
}

/// The operations of `markup`, which only `std.html` calls and handles.
const OPERATIONS: [&str; 6] = ["start", "attribute", "emit", "include", "void", "end"];

/// The messages a program is refused with.
fn refused(source: &str) -> Vec<String> {
    match lower(source.as_bytes()) {
        Ok(_) => panic!("the program is accepted:\n{source}"),
        Err(errors) => errors.into_iter().map(|e| e.message).collect(),
    }
}

/// No function writes a text as HTML: an operation of `markup` is not imported
/// by a program, and the effect is not handled by one, so `render` is the only
/// handler.
fn an_operation_of_markup_is_refused_outside_std_html_to_import_and_markup_is_refused_to_handle() {
    for operation in OPERATIONS {
        let source = format!("use std.html ({operation})\n\nfn main() {{\n}}\n");
        let said = refused(&source);
        assert!(
            said.iter().any(|m| m.contains(&format!("`{operation}`")) && m.contains("private")),
            "{operation}: {said:?}"
        );
    }
    let handler = "use std.html (markup)

fn main() {
  with markup(start: fn(tag) { }, attribute: fn(name, value) { }, emit: fn(text) { }, include: fn(page) { }, void: { }, end: fn(tag) { })
}
";
    let said = refused(handler);
    assert!(said.iter().any(|m| m.contains("`markup`") && m.contains("private")), "{said:?}");
    // Every operation is named as one that is private, so none was left out.
    for operation in OPERATIONS {
        assert!(said.iter().any(|m| m.contains(&format!("`{operation}`"))), "{operation}: {said:?}");
    }
}
