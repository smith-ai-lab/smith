//! `std.json` reads JSON text into a value that has `Deserialize`, with the
//! settings `knowledge/library/json.md` gives.
//!
//! The generator builds a type, to a depth of four, from numbers, `bool`,
//! `str`, `()`, `Option`, lists, tuples, structs, and enums, and a value of it.
//! The test writes the value as text itself, from the value and the settings,
//! and keeps where each part of the value is in the text and the way to it. A
//! text is broken in one place, and the test says what the reader must say of
//! it: the line, the column, the path, and what is wrong, from where it broke
//! the text. A text of any shape is also checked against a reader of JSON
//! written here, which says where a text first stops being JSON.

use smith::{lower, scalar};

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "every_example_of_the_reading_part_of_the_page_gives_what_the_page_says",
        every_example_of_the_reading_part_of_the_page_gives_what_the_page_says,
    ),
    (
        "a_value_written_by_to_json_as_is_read_back_as_the_value_for_each_setting",
        a_value_written_by_to_json_as_is_read_back_as_the_value_for_each_setting,
    ),
    (
        "spaces_line_breaks_escapes_and_signs_do_not_change_the_value_that_is_read",
        spaces_line_breaks_escapes_and_signs_do_not_change_the_value_that_is_read,
    ),
    (
        "a_text_broken_in_one_place_is_the_error_of_that_place_with_its_line_column_and_path",
        a_text_broken_in_one_place_is_the_error_of_that_place_with_its_line_column_and_path,
    ),
    (
        "cases_in_the_values_of_cases_are_read_to_any_depth",
        cases_in_the_values_of_cases_are_read_to_any_depth,
    ),
    (
        "the_members_of_an_object_are_read_in_any_order_with_the_tag_wherever_it_is",
        the_members_of_an_object_are_read_in_any_order_with_the_tag_wherever_it_is,
    ),
    (
        "a_member_that_is_added_is_ignored_or_refused_as_the_setting_says",
        a_member_that_is_added_is_ignored_or_refused_as_the_setting_says,
    ),
    (
        "text_the_reader_of_the_test_refuses_is_refused_where_it_refuses_it",
        text_the_reader_of_the_test_refuses_is_refused_where_it_refuses_it,
    ),
    (
        "a_rawjson_holds_the_bytes_of_its_value_and_reads_later_to_the_value_a_direct_read_gives",
        a_rawjson_holds_the_bytes_of_its_value_and_reads_later_to_the_value_a_direct_read_gives,
    ),
    (
        "a_rawjson_shares_the_memory_of_the_input",
        a_rawjson_shares_the_memory_of_the_input,
    ),
    (
        "reading_asks_for_memory_for_the_texts_and_lists_of_the_value_and_for_nothing_else",
        reading_asks_for_memory_for_the_texts_and_lists_of_the_value_and_for_nothing_else,
    ),
    (
        "a_rawjson_that_is_read_and_written_is_the_bytes_it_was_less_the_space_round_it",
        a_rawjson_that_is_read_and_written_is_the_bytes_it_was_less_the_space_round_it,
    ),
];

/// What a program prints, which is none of it when the program is refused.
fn prints(source: &str) -> Vec<u8> {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{source}"));
    scalar::run(&program).stdout
}

/// The lines a program printed.
fn lines(printed: &[u8]) -> Vec<String> {
    let text = String::from_utf8(printed.to_vec()).expect("text");
    let mut lines: Vec<String> = text.split('\n').map(str::to_string).collect();
    assert_eq!(lines.pop().as_deref(), Some(""), "the output ends with a line break");
    lines
}

/// Source that turns bytes into a text, which a literal cannot do for a byte
/// below a space other than a line break and a tab.
const TEXT: &str = "fn s(bytes: take List[u8]) -> str {
  match utf8(freeze(bytes)) {
    .ok(t) { t }
    .err(e) { \"\" }
  }
}

fn say(e: ReadError) -> str ! alloc {
  p = match e.problem {
    .malformed { \"malformed\" }
    .missing(n) { \"missing {n}\" }
    .shape((wanted, found)) { \"shape {wanted}|{found}\" }
    .range(of) { \"range {of}\" }
    .case(n) { \"case {n}\" }
    .unknown(n) { \"unknown {n}\" }
    .twice(n) { \"twice {n}\" }
  }
  \"{e.line}:{e.column} {e.path} {p}\"
}

";

/// The source of a text as a `str`.
fn text_source(bytes: &[u8]) -> String {
    let list: Vec<String> = bytes.iter().map(|b| b.to_string()).collect();
    format!("s([{}])", list.join(", "))
}

// The settings.

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct Settings {
    /// The name of the tag, when a case is written with one.
    tag: Option<&'static str>,
    camel: bool,
}

const SETTINGS: [Settings; 4] = [
    Settings { tag: None, camel: false },
    Settings { tag: Some("type"), camel: false },
    Settings { tag: None, camel: true },
    Settings { tag: Some("type"), camel: true },
];

impl Settings {
    fn source(self, refuse: bool) -> String {
        let cases = match self.tag {
            None => ".key".to_string(),
            Some(t) => format!(".tag(\"{t}\")"),
        };
        let names = if self.camel { ".camel" } else { ".written" };
        let unknown = if refuse { ", unknown: .refuse" } else { "" };
        format!("Json(cases: {cases}, names: {names}{unknown})")
    }
}

/// A name with each `_` that follows something other than `_` and precedes
/// a lower case letter taken out, and the letter in upper case.
fn camel(name: &str) -> String {
    let b = name.as_bytes();
    let mut out = String::new();
    let mut up = false;
    for i in 0..b.len() {
        let c = b[i] as char;
        if up {
            out.push(c.to_ascii_uppercase());
            up = false;
        } else if c == '_'
            && (i == 0 || b[i - 1] != b'_')
            && i + 1 < b.len()
            && b[i + 1].is_ascii_lowercase()
        {
            up = true;
        } else {
            out.push(c);
        }
    }
    out
}

// A type of the generator.

#[derive(Clone, Debug, PartialEq, Eq)]
enum Ty {
    Int(&'static str),
    Bool,
    Text,
    Unit,
    Option(Box<Ty>),
    List(Box<Ty>),
    Tuple(Vec<Ty>),
    Struct(usize),
    Enum(usize),
}

/// The whole number types, with each one's least and greatest values.
const INTS: &[(&str, i128, i128)] = &[
    ("i8", -128, 127),
    ("i16", -32768, 32767),
    ("i32", -2147483648, 2147483647),
    ("i64", i64::MIN as i128, i64::MAX as i128),
    ("u8", 0, 255),
    ("u16", 0, 65535),
    ("u32", 0, 4294967295),
    ("u64", 0, u64::MAX as i128),
];

/// The names of the fields of a struct, by their place: with and without
/// an underscore, so that a setting that changes names has names to change.
const FIELDS: [&str; 3] = ["first_name", "plain", "a_b_c"];

#[derive(Default, Debug)]
struct Defs {
    structs: Vec<Vec<Ty>>,
    enums: Vec<Vec<Option<Ty>>>,
}

impl Defs {
    fn name(&self, t: &Ty) -> String {
        match t {
            Ty::Int(n) => n.to_string(),
            Ty::Bool => "bool".to_string(),
            Ty::Text => "str".to_string(),
            Ty::Unit => "()".to_string(),
            Ty::Option(t) => format!("Option[{}]", self.name(t)),
            Ty::List(t) => format!("List[{}]", self.name(t)),
            Ty::Tuple(of) => {
                let of: Vec<String> = of.iter().map(|t| self.name(t)).collect();
                format!("({})", of.join(", "))
            }
            Ty::Struct(s) => format!("S{s}"),
            Ty::Enum(e) => format!("E{e}"),
        }
    }

    fn source(&self) -> String {
        let mut out = String::new();
        for (s, fields) in self.structs.iter().enumerate() {
            out.push_str(&format!("struct S{s} {{\n"));
            for (f, t) in fields.iter().enumerate() {
                out.push_str(&format!("  {}: {}\n", FIELDS[f], self.name(t)));
            }
            out.push_str("}\n\n");
        }
        for (e, cases) in self.enums.iter().enumerate() {
            out.push_str(&format!("enum E{e} {{\n"));
            for (c, held) in cases.iter().enumerate() {
                match held {
                    Some(t) => out.push_str(&format!("  c{c}: {}\n", self.name(t))),
                    None => out.push_str(&format!("  c{c}\n")),
                }
            }
            out.push_str("}\n\n");
        }
        out
    }
}

/// A type that is not `null` as JSON, so that a missing value and a value that
/// is there are not written alike when it is in an `Option`.
fn writes_null(t: &Ty) -> bool {
    matches!(t, Ty::Unit | Ty::Option(_))
}

fn ty(g: &mut Gen, defs: &mut Defs, depth: usize) -> Ty {
    let kinds = if depth == 0 { 4 } else { 9 };
    match g.below(kinds) {
        0 => Ty::Bool,
        1 => Ty::Int(g.pick(INTS).0),
        2 => Ty::Text,
        3 => Ty::Unit,
        4 => {
            let inner = ty(g, defs, depth - 1);
            Ty::Option(Box::new(if writes_null(&inner) { Ty::Bool } else { inner }))
        }
        5 => {
            // A list of `()` is not built.
            let element = match ty(g, defs, depth - 1) {
                Ty::Unit => Ty::Bool,
                other => other,
            };
            Ty::List(Box::new(element))
        }
        6 => {
            let size = 2 + g.below(2);
            Ty::Tuple((0..size).map(|_| ty(g, defs, depth - 1)).collect())
        }
        7 => {
            let size = 1 + g.below(3);
            let fields: Vec<Ty> = (0..size).map(|_| ty(g, defs, depth - 1)).collect();
            defs.structs.push(fields);
            Ty::Struct(defs.structs.len() - 1)
        }
        _ => {
            let size = 1 + g.below(3);
            let mut cases = Vec::new();
            for _ in 0..size {
                let held = g.one_in(2);
                cases.push(held.then(|| match ty(g, defs, depth - 1) {
                    Ty::Unit => Ty::Bool,
                    other => other,
                }));
            }
            defs.enums.push(cases);
            Ty::Enum(defs.enums.len() - 1)
        }
    }
}

/// The texts a value of `str` is, as the bytes they are.
const TEXTS: &[&str] = &[
    "",
    "a",
    "hi there",
    "say \"hi\"",
    "back\\slash",
    "line\nbreak\ttab",
    "é€😀",
];

/// A value of a type, which is what the generator knows of it.
#[derive(Clone, Debug)]
enum Val {
    Bool(bool),
    Int(i128),
    Text(Vec<u8>),
    Unit,
    Option(Option<Box<Val>>),
    List(Vec<Val>),
    Tuple(Vec<Val>),
    Struct(usize, Vec<Val>),
    Enum(usize, usize, Option<Box<Val>>),
}

fn value(g: &mut Gen, defs: &Defs, t: &Ty) -> Val {
    match t {
        Ty::Bool => Val::Bool(g.one_in(2)),
        Ty::Int(name) => {
            let (_, least, greatest) = *INTS.iter().find(|(n, ..)| n == name).unwrap();
            Val::Int(*g.pick(&[0, 1, least, greatest, 7]))
        }
        Ty::Text => Val::Text(g.pick(TEXTS).as_bytes().to_vec()),
        Ty::Unit => Val::Unit,
        Ty::Option(of) => {
            if g.one_in(2) {
                Val::Option(None)
            } else {
                Val::Option(Some(Box::new(value(g, defs, of))))
            }
        }
        Ty::List(of) => {
            let size = g.below(4);
            Val::List((0..size).map(|_| value(g, defs, of)).collect())
        }
        Ty::Tuple(of) => Val::Tuple(of.iter().map(|t| value(g, defs, t)).collect()),
        Ty::Struct(s) => Val::Struct(*s, defs.structs[*s].iter().map(|t| value(g, defs, t)).collect()),
        Ty::Enum(e) => {
            let cases = &defs.enums[*e];
            let c = g.below(cases.len());
            Val::Enum(*e, c, cases[c].as_ref().map(|t| Box::new(value(g, defs, t))))
        }
    }
}

impl Val {
    fn source(&self) -> String {
        let many = |of: &[Val]| of.iter().map(Val::source).collect::<Vec<_>>().join(", ");
        match self {
            Val::Bool(b) => b.to_string(),
            Val::Int(n) => n.to_string(),
            Val::Text(t) => text_source(t),
            Val::Unit => "()".to_string(),
            Val::Option(None) => "Option.none".to_string(),
            Val::Option(Some(v)) => format!("Option.some({})", v.source()),
            Val::List(of) => format!("[{}]", many(of)),
            Val::Tuple(of) => format!("({})", many(of)),
            Val::Struct(s, of) => {
                let fields: Vec<String> = of
                    .iter()
                    .enumerate()
                    .map(|(f, v)| format!("{}: {}", FIELDS[f], v.source()))
                    .collect();
                format!("S{s}({})", fields.join(", "))
            }
            Val::Enum(e, c, None) => format!("E{e}.c{c}"),
            Val::Enum(e, c, Some(v)) => format!("E{e}.c{c}({})", v.source()),
        }
    }
}

// The text of a value, with where each part of it is.

/// How a text is written beyond the compact form: what is put around each
/// bracket, comma, and colon, whether each character of a text is an escape,
/// and whether a zero is written with a sign.
#[derive(Clone, Copy)]
struct Style {
    pad: &'static str,
    escape: bool,
    negative_zero: bool,
}

const COMPACT: Style = Style { pad: "", escape: false, negative_zero: false };

/// A part of the value: its type, where it is in the text, and the way to it.
#[derive(Clone, Debug)]
struct Node {
    ty: Ty,
    start: usize,
    end: usize,
    path: String,
}

/// A member of an object: its name in the text, where its name starts and its
/// value ends, and, for the member of a struct, the field it is.
#[derive(Clone, Debug)]
struct Member {
    key: String,
    key_start: usize,
    end: usize,
    field: Option<(&'static str, bool)>,
}

/// An object that holds the fields of a struct, or a case that is written with
/// a tag.
#[derive(Clone, Debug)]
struct Obj {
    start: usize,
    end: usize,
    path: String,
    members: Vec<Member>,
}

struct Written<'a> {
    bytes: Vec<u8>,
    nodes: Vec<Node>,
    objs: Vec<Obj>,
    defs: &'a Defs,
    settings: Settings,
    style: Style,
}

fn join(path: &str, key: &str) -> String {
    if path.is_empty() { key.to_string() } else { format!("{path}.{key}") }
}

fn quoted(bytes: &[u8], escape: bool, out: &mut Vec<u8>) {
    out.push(b'"');
    if escape {
        for c in String::from_utf8(bytes.to_vec()).unwrap().chars() {
            let mut units = [0u16; 2];
            for u in c.encode_utf16(&mut units) {
                out.extend_from_slice(format!("\\u{u:04x}").as_bytes());
            }
        }
    } else {
        for &b in bytes {
            match b {
                b'"' => out.extend_from_slice(b"\\\""),
                b'\\' => out.extend_from_slice(b"\\\\"),
                b'\n' => out.extend_from_slice(b"\\n"),
                b'\t' => out.extend_from_slice(b"\\t"),
                b'\r' => out.extend_from_slice(b"\\r"),
                8 => out.extend_from_slice(b"\\b"),
                12 => out.extend_from_slice(b"\\f"),
                b if b < 0x20 => out.extend_from_slice(format!("\\u{b:04x}").as_bytes()),
                b => out.push(b),
            }
        }
    }
    out.push(b'"');
}

impl Obj {
    /// Whether the object holds the fields of a struct, so that a member can be
    /// added to it, and each of its members is a field or the tag.
    fn has_fields(&self) -> bool {
        self.members.iter().any(|m| m.field.is_some())
    }
}

impl<'a> Written<'a> {
    fn new(defs: &'a Defs, settings: Settings, style: Style) -> Self {
        Written { bytes: Vec::new(), nodes: Vec::new(), objs: Vec::new(), defs, settings, style }
    }

    fn put(&mut self, text: &str) {
        self.bytes.extend_from_slice(text.as_bytes());
    }

    fn pad(&mut self) {
        let pad = self.style.pad;
        self.put(pad);
    }

    fn open(&mut self, b: char) {
        self.put(&b.to_string());
        self.pad();
    }

    fn close(&mut self, b: char) {
        self.pad();
        self.put(&b.to_string());
    }

    fn separate(&mut self, b: char) {
        self.pad();
        self.put(&b.to_string());
        self.pad();
    }

    fn key(&mut self, name: &str) {
        let escape = self.style.escape;
        quoted(name.as_bytes(), escape, &mut self.bytes);
    }

    fn field_key(&self, f: usize) -> String {
        if self.settings.camel { camel(FIELDS[f]) } else { FIELDS[f].to_string() }
    }

    /// The members of a struct, after `written` members that are already in
    /// the object.
    fn members(&mut self, s: usize, vals: &[Val], path: &str, members: &mut Vec<Member>) {
        for (f, v) in vals.iter().enumerate() {
            if !members.is_empty() {
                self.separate(',');
            }
            let key = self.field_key(f);
            let key_start = self.bytes.len();
            self.key(&key);
            self.separate(':');
            let ty = self.defs.structs[s][f].clone();
            self.emit(v, &ty, &join(path, &key));
            let end = self.bytes.len();
            members.push(Member {
                key,
                key_start,
                end,
                field: Some((FIELDS[f], matches!(ty, Ty::Option(_)))),
            });
        }
    }

    fn emit(&mut self, v: &Val, t: &Ty, path: &str) {
        let start = self.bytes.len();
        match (v, t) {
            (Val::Bool(b), _) => self.put(&b.to_string()),
            (Val::Int(n), _) => {
                if *n == 0 && self.style.negative_zero {
                    self.put("-0");
                } else {
                    self.put(&n.to_string());
                }
            }
            (Val::Text(b), _) => {
                let escape = self.style.escape;
                quoted(b, escape, &mut self.bytes);
            }
            (Val::Unit, _) | (Val::Option(None), _) => self.put("null"),
            (Val::Option(Some(x)), Ty::Option(inner)) => self.emit(x, inner, path),
            (Val::List(items), Ty::List(inner)) => {
                self.open('[');
                for (i, x) in items.iter().enumerate() {
                    if i > 0 {
                        self.separate(',');
                    }
                    self.emit(x, inner, &format!("{path}[{i}]"));
                }
                self.close(']');
            }
            (Val::Tuple(items), Ty::Tuple(tys)) => {
                self.open('[');
                for (i, x) in items.iter().enumerate() {
                    if i > 0 {
                        self.separate(',');
                    }
                    self.emit(x, &tys[i], &format!("{path}[{i}]"));
                }
                self.close(']');
            }
            (Val::Struct(s, vals), _) => {
                self.open('{');
                let mut members = Vec::new();
                self.members(*s, vals, path, &mut members);
                self.close('}');
                self.objs.push(Obj { start, end: self.bytes.len(), path: path.to_string(), members });
            }
            (Val::Enum(_, c, held), _) => {
                let name = format!("c{c}");
                match (self.settings.tag, held) {
                    (None, None) => {
                        let escape = self.style.escape;
                        quoted(name.as_bytes(), escape, &mut self.bytes);
                    }
                    (None, Some(x)) => {
                        let Ty::Enum(e) = t else { unreachable!() };
                        let ty = self.defs.enums[*e][*c].clone().unwrap();
                        self.open('{');
                        self.key(&name);
                        self.separate(':');
                        self.emit(x, &ty, &join(path, &name));
                        self.close('}');
                    }
                    (Some(tag), held) => {
                        self.open('{');
                        let mut members = vec![Member {
                            key: tag.to_string(),
                            key_start: self.bytes.len(),
                            end: 0,
                            field: None,
                        }];
                        self.key(tag);
                        self.separate(':');
                        let escape = self.style.escape;
                        quoted(name.as_bytes(), escape, &mut self.bytes);
                        members[0].end = self.bytes.len();
                        match held.as_deref() {
                            None => {}
                            Some(Val::Struct(s, vals)) => {
                                // The tag is a member, so the struct's first is not the first.
                                self.members(*s, vals, path, &mut members);
                            }
                            Some(other) => {
                                self.separate(',');
                                let key_start = self.bytes.len();
                                self.key("value");
                                self.separate(':');
                                let Ty::Enum(e) = t else { unreachable!() };
                                let ty = self.defs.enums[*e][*c].clone().unwrap();
                                self.emit(other, &ty, &join(path, "value"));
                                let end = self.bytes.len();
                                members.push(Member { key: "value".into(), key_start, end, field: None });
                            }
                        }
                        self.close('}');
                        self.objs.push(Obj { start, end: self.bytes.len(), path: path.to_string(), members });
                    }
                }
            }
            (v, t) => panic!("{v:?} is not a {t:?}"),
        }
        let end = self.bytes.len();
        self.nodes.push(Node { ty: t.clone(), start, end, path: path.to_string() });
    }
}

/// The text of a value, with the nodes and objects of it.
fn write<'a>(defs: &'a Defs, t: &Ty, v: &Val, settings: Settings, style: Style) -> Written<'a> {
    let mut w = Written::new(defs, settings, style);
    w.pad();
    w.emit(v, t, "");
    w.pad();
    w
}

// The place of a byte in a text.

/// The line and the column of the byte `at`, each counted from 1, the column
/// in bytes.
fn place(text: &[u8], at: usize) -> (usize, usize) {
    let before = &text[..at.min(text.len())];
    let line = 1 + before.iter().filter(|b| **b == b'\n').count();
    let start = before.iter().rposition(|b| *b == b'\n').map_or(0, |i| i + 1);
    (line, at - start + 1)
}

/// What a program says of an error: `say` in the program.
fn error(text: &[u8], at: usize, path: &str, problem: &str) -> String {
    let (line, column) = place(text, at);
    format!("{line}:{column} {path} {problem}")
}

// A program that reads texts into a value of one type.

/// One text to read, with the setting of `unknown` it is read with, and the
/// line the program must print for it.
#[derive(Debug)]
struct Case {
    text: Vec<u8>,
    refuse: bool,
    expected: String,
}

/// A program that reads each case and prints a line for it: `ok` and whether
/// what was read is the value, or what the error says.
fn reading(defs: &Defs, t: &Ty, v: &Val, settings: Settings, cases: &[Case]) -> String {
    let name = defs.name(t);
    let mut out = format!(
        "use std.json (from_json_as, to_json, Json)\nuse std.deserialize (ReadError, Problem)\n\n{TEXT}{}",
        defs.source()
    );
    out.push_str(&format!(
        "fn main() ! console, alloc {{\n  v: {name} = {}\n  keep = {}\n  refuse = {}\n",
        v.source(),
        settings.source(false),
        settings.source(true)
    ));
    for (i, case) in cases.iter().enumerate() {
        let json = if case.refuse { "refuse" } else { "keep" };
        out.push_str(&format!(
            "  r{i}: Result[{name}, ReadError] = from_json_as({}, {json})\n  match r{i} {{\n    .ok(w) {{ print(\"ok {{to_json(w) == to_json(v)}}\") }}\n    .err(e) {{ print(say(e)) }}\n  }}\n",
            text_source(&case.text)
        ));
    }
    out.push_str("}\n");
    out
}

fn run_cases(defs: &Defs, t: &Ty, v: &Val, settings: Settings, cases: &[Case]) {
    let source = reading(defs, t, v, settings, cases);
    let printed = lines(&prints(&source));
    assert_eq!(printed.len(), cases.len(), "{source}");
    for (i, (line, case)) in printed.iter().zip(cases).enumerate() {
        assert_eq!(
            line,
            &case.expected,
            "case {i}, {settings:?}, refuse {}, in {:?}\n{source}",
            case.refuse,
            String::from_utf8_lossy(&case.text)
        );
    }
}

/// A type, a value of it, and the settings it is written with.
fn made(g: &mut Gen) -> (Defs, Ty, Val, Settings) {
    let mut defs = Defs::default();
    let t = ty(g, &mut defs, 3);
    let v = value(g, &defs, &t);
    let settings = *g.pick(&SETTINGS);
    (defs, t, v, settings)
}

// What the issue says, as it says it.

/// One example of the page: the type that is read, the text, the settings, the
/// line that is printed when it is read, and the line the page says.
struct Example {
    ty: &'static str,
    text: String,
    json: &'static str,
    ok: &'static str,
    expected: String,
}

impl Example {
    /// The same, with the error `by` bytes later.
    fn after(mut self, by: usize) -> Self {
        let (line, column) = self.expected.split_once(": ").unwrap().0.split_once(':').unwrap();
        let (line, column): (usize, usize) = (line.parse().unwrap(), column.parse().unwrap());
        let rest = self.expected.split_once(": ").unwrap().1.to_string();
        self.expected = format!("{line}:{}: {rest}", column + by);
        self
    }
}

/// What `describe` says of an error at the byte `at` of `text`.
fn described(text: &str, at: usize, path: &str, what: &str) -> String {
    let (line, column) = place(text.as_bytes(), at);
    if path.is_empty() {
        format!("{line}:{column}: {what}")
    } else {
        format!("{line}:{column}: {path}: {what}")
    }
}

fn every_example_of_the_reading_part_of_the_page_gives_what_the_page_says() {
    let ok = |ty, text: &str, json, ok, expected: &str| Example {
        ty,
        text: text.to_string(),
        json,
        ok,
        expected: expected.to_string(),
    };
    let bad = |ty, text: &str, json, at: &str, path: &str, what: &str| Example {
        ty,
        text: text.to_string(),
        json,
        ok: "\"ok\"",
        expected: described(text, text.find(at).unwrap(), path, what),
    };
    let keep = "Json(cases: .key, names: .written)";
    let refuse = "Json(cases: .key, names: .written, unknown: .refuse)";
    let mcp = "Json(cases: .tag(\"type\"), names: .camel)";
    let tools = r#"{"tools":[{"name":"a"},{"name":"b"},{"name":"c","timeout":"3"}]}"#;
    let examples = [
        ok("Point", r#"{"x":3,"y":4}"#, keep, "\"{v.x} {v.y}\"", "3 4"),
        ok("Tool", r#"{"name":"a"}"#, keep, "\"{v.name} {opt(v.description)} {v.timeout}\"", "a none 30"),
        ok("Tool", r#"{"name":"a","description":null}"#, keep, "opt(v.description)", "none"),
        ok("Tool", r#"{"name":"a","timeout":7,"extra":[1,{"a":null}]}"#, keep, "\"{v.timeout}\"", "7"),
        bad("Tools", tools, keep, "\"3\"", "tools[2].timeout", "expected a whole number, found a text"),
        ok("Shape", "\"empty\"", keep, "\"{v == Shape.empty}\"", "true"),
        ok("Shape", r#"{"square":4}"#, keep, "\"{v == Shape.square(4)}\"", "true"),
        ok("Shape", r#"{"circle":{"radius":2}}"#, keep, "\"{v == Shape.circle(Circle(radius: 2))}\"", "true"),
        ok("Shape", r#"{"type":"circle","radius":2}"#, mcp, "\"{v == Shape.circle(Circle(radius: 2))}\"", "true"),
        ok("Shape", r#"{"type":"square","value":4}"#, mcp, "\"{v == Shape.square(4)}\"", "true"),
        ok("Shape", r#"{"type":"empty"}"#, mcp, "\"{v == Shape.empty}\"", "true"),
        ok("Content", r#"{"inputSchema":1}"#, mcp, "\"{v.input_schema}\"", "1"),
        bad("u8", "300", keep, "300", "", "a number that does not fit u8"),
        bad("u64", "\"5\"", keep, "\"5\"", "", "expected a whole number, found a text"),
        bad("u64", "5.5", keep, "5.5", "", "expected a whole number, found a number that is not whole"),
        bad("u64", "18446744073709551616", keep, "1844", "", "a number that does not fit u64"),
        bad("i64", "-9223372036854775809", keep, "-92", "", "a number that does not fit i64"),
        ok("()", "null", keep, "\"unit\"", "unit"),
        bad("Point", "null", keep, "null", "", "expected an object, found null"),
        bad("Point", r#"{"x":1,"x":2,"y":3}"#, keep, "\"x\":2", "x", "`x` is given twice"),
        bad("Point", r#"{"x":1}"#, keep, "}", "", "missing `y`"),
        bad("Point", r#"{"x":1,"y":3,"z":0}"#, refuse, "\"z\"", "z", "unknown `z`"),
        ok("Point", r#"{"x":1,"y":3,"z":0}"#, keep, "\"{v.x}\"", "1"),
        bad("Point", r#"{"x":1,"y":3,"z":0,"z":1}"#, keep, "\"z\":1", "z", "`z` is given twice"),
        bad("Shape", r#"{"triangle":1}"#, keep, "\"triangle\"", "triangle", "`triangle` is not a case"),
        ok("Shape", r#"{"radius":2,"type":"circle"}"#, mcp, "\"{v == Shape.circle(Circle(radius: 2))}\"", "true"),
        ok("Shape", r#"{"value":4,"type":"square"}"#, mcp, "\"{v == Shape.square(4)}\"", "true"),
        bad("Shape", r#"{"radius":2}"#, mcp, "}", "", "missing `type`"),
        bad("Content", r#"{"input_schema":1}"#, mcp, "}", "", "missing `input_schema`"),
        bad("Point", r#"{"x":1,"y":3} x"#, keep, " x", "", "not valid JSON").after(1),
    ];
    let mut source = format!(
        "use std.json (from_json_as, Json, describe)
use std.deserialize (ReadError, Problem)

{TEXT}struct Point {{
  x: i64
  y: i64
}}

struct Tool {{
  name: str
  description: Option[str]
  timeout: u64 = 30
}}

struct Tools {{
  tools: List[Tool]
}}

struct Circle {{
  radius: u64
}}

enum Shape {{
  circle: Circle
  square: u64
  empty
}}

struct Content {{
  input_schema: u8
}}

fn opt(o: Option[str]) -> str {{
  match o {{
    .none {{ \"none\" }}
    .some(t) {{ t }}
  }}
}}

fn main() ! console, alloc {{
"
    );
    for (i, e) in examples.iter().enumerate() {
        source.push_str(&format!(
            "  r{i}: Result[{}, ReadError] = from_json_as({}, {})\n  match r{i} {{\n    .ok(v) {{ print({}) }}\n    .err(e) {{ print(describe(e)) }}\n  }}\n",
            e.ty,
            text_source(e.text.as_bytes()),
            e.json,
            e.ok
        ));
    }
    source.push_str("}\n");
    let printed = lines(&prints(&source));
    let expected: Vec<&str> = examples.iter().map(|e| e.expected.as_str()).collect();
    assert_eq!(printed, expected, "{source}");
}

// A value is read back from what was written.

fn a_value_written_by_to_json_as_is_read_back_as_the_value_for_each_setting() {
    generate::check(6001, 6, made_with_defs, |(defs, t, v, _)| {
        let name = defs.name(t);
        let mut source = format!(
            "use std.json (to_json_as, from_json_as, Json)\nuse std.deserialize (ReadError, Problem)\n\n{TEXT}{}fn main() ! console, alloc {{\n  v: {name} = {}\n",
            defs.source(),
            v.source()
        );
        for (k, settings) in SETTINGS.iter().enumerate() {
            source.push_str(&format!(
                "  j{k} = {}\n  match to_json_as(v, j{k}) {{\n    .ok(t{k}) {{\n      r{k}: Result[{name}, ReadError] = from_json_as(t{k}, j{k})\n      match r{k} {{\n        .ok(w) {{\n          match to_json_as(w, j{k}) {{\n            .ok(u{k}) {{ print(\"ok {{u{k} == t{k}}}\") }}\n            .err(c) {{ print(\"clash\") }}\n          }}\n        }}\n        .err(e) {{ print(say(e)) }}\n      }}\n    }}\n    .err(c) {{ print(\"clash\") }}\n  }}\n",
                settings.source(false)
            ));
        }
        source.push_str("}\n");
        let printed = lines(&prints(&source));
        assert_eq!(printed, vec!["ok true"; SETTINGS.len()], "{source}");
    });
}

/// A type, a value of it, and a setting, with the definitions they need.
fn made_with_defs(g: &mut Gen) -> (Defs, Ty, Val, Settings) {
    made(g)
}

fn spaces_line_breaks_escapes_and_signs_do_not_change_the_value_that_is_read() {
    let styles = [
        Style { pad: " ", escape: false, negative_zero: false },
        Style { pad: "\n  ", escape: false, negative_zero: false },
        Style { pad: "", escape: true, negative_zero: true },
        Style { pad: "\r\n\t ", escape: true, negative_zero: true },
    ];
    generate::check(6002, 6, made_with_defs, |(defs, t, v, settings)| {
        let cases: Vec<Case> = styles
            .iter()
            .map(|style| Case {
                text: write(defs, t, v, *settings, *style).bytes,
                refuse: false,
                expected: "ok true".to_string(),
            })
            .collect();
        run_cases(defs, t, v, *settings, &cases);
    });
}

// The members of an object are in any order.

fn the_members_of_an_object_are_read_in_any_order_with_the_tag_wherever_it_is() {
    generate::check(6007, 8, |g: &mut Gen| {
        let (defs, t, v, settings) = made(g);
        let w = write(&defs, &t, &v, settings, COMPACT);
        let mut cases = vec![];
        for o in w.objs.iter().take(4) {
            // The members of the object, in an order that the generator draws.
            let mut order: Vec<usize> = (0..o.members.len()).collect();
            for i in (1..order.len()).rev() {
                order.swap(i, g.below(i + 1));
            }
            let members: Vec<&[u8]> =
                order.iter().map(|i| &w.bytes[o.members[*i].key_start..o.members[*i].end]).collect();
            let body = [&b"{"[..], &members.join(&b","[..]), &b"}"[..]].concat();
            let text = spliced(&w.bytes, o.start, o.end, &body);
            cases.push(Case { text, refuse: false, expected: "ok true".into() });
        }
        (defs, t, v, settings, cases)
    }, |(defs, t, v, settings, cases)| {
        if !cases.is_empty() {
            run_cases(defs, t, v, *settings, cases)
        }
    });
}

// Cases in the values of cases.

fn cases_in_the_values_of_cases_are_read_to_any_depth() {
    // A type of `depth` enums, each of which holds the next in a case and ends
    // in a case that holds nothing, and the text of the value that goes to the
    // end, for each setting, with the tag before the value and after it by turns.
    for depth in [3usize, 5, 70] {
        let mut types = String::new();
        for d in 0..depth {
            let held = if d + 1 < depth { format!("c0: E{}\n  c1", d + 1) } else { "c0\n  c1".to_string() };
            types.push_str(&format!("enum E{d} {{\n  {held}\n}}\n\n"));
        }
        let mut value: String = (0..depth - 1).map(|d| format!("E{d}.c0(")).collect();
        value.push_str(&format!("E{}.c0", depth - 1));
        value.push_str(&")".repeat(depth - 1));
        // The text with a case as a key, and with it as a tag.
        let key = format!("{}\"c0\"{}", "{\"c0\":".repeat(depth - 1), "}".repeat(depth - 1));
        let tag_first = format!("{}{{\"type\":\"c0\"}}{}", "{\"type\":\"c0\",\"value\":".repeat(depth - 1), "}".repeat(depth - 1));
        let mut tag_last = String::new();
        for _ in 0..depth - 1 {
            tag_last.push_str("{\"value\":");
        }
        tag_last.push_str("{\"type\":\"c0\"}");
        for _ in 0..depth - 1 {
            tag_last.push_str(",\"type\":\"c0\"}");
        }
        let mut source = format!(
            "use std.json (from_json_as, to_json_as, Json)\nuse std.deserialize (ReadError, Problem)\n\n{TEXT}{types}fn main() ! console, alloc {{\n  v: E0 = {value}\n  k = Json(cases: .key, names: .written)\n  t = Json(cases: .tag(\"type\"), names: .written)\n"
        );
        for (name, text, json) in [("key", &key, "k"), ("first", &tag_first, "t"), ("last", &tag_last, "t")] {
            source.push_str(&format!(
                "  {name}: Result[E0, ReadError] = from_json_as({}, {json})\n  match {name} {{\n    .ok(w) {{\n      match to_json_as(w, {json}) {{\n        .ok(u) {{\n          match to_json_as(v, {json}) {{\n            .ok(x) {{ print(\"ok {{u == x}}\") }}\n            .err(c) {{ print(\"clash\") }}\n          }}\n        }}\n        .err(c) {{ print(\"clash\") }}\n      }}\n    }}\n    .err(e) {{ print(say(e)) }}\n  }}\n",
                text_source(text.as_bytes())
            ));
        }
        source.push_str("}\n");
        let printed = lines(&prints(&source));
        assert_eq!(printed.len(), 3, "{source}");
        assert_eq!(printed[0], "ok true", "{source}");
        assert_eq!(printed[1], "ok true", "{source}");
        assert_eq!(printed[2], "ok true", "{source}");
    }
}

// A text broken in one place.

/// What a reader says it wanted of a value of the type, when the value there
/// is not one.
fn wanted(t: &Ty, settings: Settings) -> &'static str {
    match t {
        Ty::Bool => "a boolean",
        Ty::Int(_) => "a whole number",
        Ty::Text => "a text",
        Ty::List(_) | Ty::Tuple(_) => "a list",
        Ty::Struct(_) => "an object",
        Ty::Enum(_) if settings.tag.is_some() => "an object",
        Ty::Enum(_) => "a case",
        Ty::Unit | Ty::Option(_) => unreachable!("a value that may be null"),
    }
}

/// The text with the bytes from `start` up to `end` taken out and `with` put
/// in their place.
fn spliced(text: &[u8], start: usize, end: usize, with: &[u8]) -> Vec<u8> {
    [&text[..start], with, &text[end..]].concat()
}

/// Cases made by breaking a text in one place each, for the kinds in `kinds`.
fn broken(g: &mut Gen, w: &Written, settings: Settings) -> Vec<Case> {
    let text = &w.bytes;
    let mut cases = Vec::new();
    let mut add = |text: Vec<u8>, at: usize, path: &str, problem: &str| {
        let expected = error(&text, at, path, problem);
        cases.push(Case { text, refuse: false, expected });
    };
    // A value that is not `null` where `null` is read as missing: not an
    // `Option` or `()`, and not what an `Option` holds.
    let plain: Vec<&Node> = (w.nodes.iter())
        .filter(|n| !matches!(n.ty, Ty::Option(_) | Ty::Unit))
        .filter(|n| {
            !(w.nodes.iter())
                .any(|o| matches!(o.ty, Ty::Option(_)) && o.start == n.start && o.end == n.end)
        })
        .collect();
    let ints: Vec<&Node> = plain.iter().copied().filter(|n| matches!(n.ty, Ty::Int(_))).collect();
    // A `null` for a value that is not an `Option` or `()`.
    if !plain.is_empty() {
        let n = *g.pick(&plain);
        let t = spliced(text, n.start, n.end, b"null");
        add(t, n.start, &n.path, &format!("shape {}|null", wanted(&n.ty, settings)));
    }
    if !ints.is_empty() {
        // A text where a number is needed.
        let n = *g.pick(&ints);
        let t = spliced(text, n.start, n.end, b"\"5\"");
        add(t, n.start, &n.path, &format!("shape {}|a text", wanted(&n.ty, settings)));
        // A fraction, and an exponent.
        let n = *g.pick(&ints);
        let with: &[u8] = if g.one_in(2) { b"1.5" } else { b"2e3" };
        let t = spliced(text, n.start, n.end, with);
        add(t, n.start, &n.path, "shape a whole number|a number that is not whole");
        // A number the type cannot hold.
        let n = *g.pick(&ints);
        let Ty::Int(name) = &n.ty else { unreachable!() };
        let (_, least, greatest) = *INTS.iter().find(|(m, ..)| m == name).unwrap();
        let too = if least < 0 && g.one_in(2) { least - 1 } else { greatest + 1 };
        let t = spliced(text, n.start, n.end, too.to_string().as_bytes());
        add(t, n.start, &n.path, &format!("range {name}"));
    }
    // A bracket changed, where a value opens and where it closes.
    let bracketed: Vec<&Node> = plain
        .iter()
        .copied()
        .filter(|n| matches!(text[n.start], b'[' | b'{') && !matches!(n.ty, Ty::Int(_)))
        .collect();
    if !bracketed.is_empty() {
        let n = *g.pick(&bracketed);
        let (to, found) = if text[n.start] == b'[' { (b'{', "an object") } else { (b'[', "a list") };
        let t = spliced(text, n.start, n.start + 1, &[to]);
        add(t, n.start, &n.path, &format!("shape {}|{found}", wanted(&n.ty, settings)));
        let n = *g.pick(&bracketed);
        let to = if text[n.end - 1] == b']' { b'}' } else { b']' };
        let t = spliced(text, n.end - 1, n.end, &[to]);
        add(t, n.end - 1, &n.path, "malformed");
    }
    // A member repeated, and a member removed.
    let with_fields: Vec<&Obj> = w.objs.iter().filter(|o| o.has_fields()).collect();
    if !with_fields.is_empty() {
        let o = *g.pick(&with_fields);
        let fields: Vec<&Member> = o.members.iter().filter(|m| m.field.is_some()).collect();
        let m = *g.pick(&fields);
        let (declared, _) = m.field.unwrap();
        let copy = [&b","[..], &text[m.key_start..m.end]].concat();
        let t = spliced(text, m.end, m.end, &copy);
        add(t, m.end + 1, &join(&o.path, &m.key), &format!("twice {declared}"));
        let required: Vec<usize> = (0..o.members.len())
            .filter(|i| matches!(o.members[*i].field, Some((_, false))))
            .collect();
        if !required.is_empty() {
            let i = required[g.below(required.len())];
            let m = &o.members[i];
            let (declared, _) = m.field.unwrap();
            let (start, end) = if i > 0 {
                (o.members[i - 1].end, m.end)
            } else if o.members.len() > 1 {
                (m.key_start, o.members[1].key_start)
            } else {
                (m.key_start, m.end)
            };
            let t = spliced(text, start, end, b"");
            add(t, o.end - 1 - (end - start), &o.path, &format!("missing {declared}"));
        }
    }
    // The tag taken out of an object, which has none then.
    if let Some(tag) = settings.tag {
        let tagged: Vec<&Obj> = (w.objs.iter())
            .filter(|o| o.members[0].field.is_none() && o.members[0].key == tag)
            .collect();
        if !tagged.is_empty() {
            let o = *g.pick(&tagged);
            let (start, end) = if o.members.len() > 1 {
                (o.members[0].key_start, o.members[1].key_start)
            } else {
                (o.members[0].key_start, o.members[0].end)
            };
            let t = spliced(text, start, end, b"");
            add(t, o.end - 1 - (end - start), &o.path, &format!("missing {tag}"));
        }
    }
    cases
}

fn a_text_broken_in_one_place_is_the_error_of_that_place_with_its_line_column_and_path() {
    generate::check(6003, 8, |g: &mut Gen| {
        let (defs, t, v, settings) = made(g);
        let w = write(&defs, &t, &v, settings, COMPACT);
        let mut cases = vec![Case { text: w.bytes.clone(), refuse: false, expected: "ok true".into() }];
        cases.extend(broken(g, &w, settings));
        (defs, t, v, settings, cases)
    }, |(defs, t, v, settings, cases)| run_cases(defs, t, v, *settings, cases));
}

fn a_member_that_is_added_is_ignored_or_refused_as_the_setting_says() {
    let added: [&[u8]; 5] = [b"1", b"\"a\\n\"", b"[1,{\"q\":null},[]]", b"{}", b"null"];
    generate::check(6004, 8, |g: &mut Gen| {
        let (defs, t, v, settings) = made(g);
        let w = write(&defs, &t, &v, settings, COMPACT);
        let mut cases = Vec::new();
        for o in w.objs.iter().filter(|o| o.has_fields()).take(3) {
            let member = [&b",\"zz\":"[..], g.pick(&added)].concat();
            let text = spliced(&w.bytes, o.end - 1, o.end - 1, &member);
            let at = o.end;
            let expected = error(&text, at, &join(&o.path, "zz"), "unknown zz");
            cases.push(Case { text: text.clone(), refuse: false, expected: "ok true".into() });
            cases.push(Case { text, refuse: true, expected });
            // A name that is not the name that is written is a member that
            // the type has no field of.
            if settings.camel {
                let renamed: Vec<&Member> = (o.members.iter())
                    .filter(|m| m.field.is_some_and(|(declared, _)| declared != m.key))
                    .collect();
                if !renamed.is_empty() {
                    let m = *g.pick(&renamed);
                    let (declared, _) = m.field.unwrap();
                    let name = if g.one_in(2) {
                        declared.to_string()
                    } else {
                        declared.replace("_a", "_A").replace("_b", "_B").replace("_c", "_C").replace("_n", "_N")
                    };
                    let key = format!("\"{}\"", m.key);
                    let text = spliced(&w.bytes, m.key_start, m.key_start + key.len(), format!("\"{name}\"").as_bytes());
                    let expected = error(&text, m.key_start, &join(&o.path, &name), &format!("unknown {name}"));
                    cases.push(Case { text, refuse: true, expected });
                }
            }
        }
        (defs, t, v, settings, cases)
    }, |(defs, t, v, settings, cases)| {
        if !cases.is_empty() {
            run_cases(defs, t, v, *settings, cases)
        }
    });
}

// A reader of JSON, which says where a text first stops being JSON.

#[derive(Clone, Debug)]
enum Tree {
    Null,
    Bool(bool),
    Number(&'static str),
    Text(Vec<u8>),
    Array(Vec<Tree>),
    Object(Vec<(&'static str, Tree)>),
}

const NUMBERS: &[&str] = &[
    "0", "-0", "7", "-12", "1.5", "-0.25", "1e3", "2E-2", "10.5e+2", "123456789012345678901234567890",
];
const KEYS: &[&str] = &["a", "b_c", "x"];

fn tree(g: &mut Gen, depth: usize) -> Tree {
    match g.below(if depth == 0 { 4 } else { 6 }) {
        0 => Tree::Null,
        1 => Tree::Bool(g.one_in(2)),
        2 => Tree::Number(g.pick(NUMBERS)),
        3 => Tree::Text(g.pick(TEXTS).as_bytes().to_vec()),
        4 => Tree::Array((0..g.below(3)).map(|_| tree(g, depth - 1)).collect()),
        _ => Tree::Object((0..g.below(3)).map(|i| (KEYS[i], tree(g, depth - 1))).collect()),
    }
}

/// The text of a tree, with `pad` around its brackets, commas, and colons, and
/// each text written with escapes when `escape` says so.
fn tree_text(t: &Tree, pad: &str, escape: bool, out: &mut Vec<u8>) {
    match t {
        Tree::Null => out.extend_from_slice(b"null"),
        Tree::Bool(b) => out.extend_from_slice(b.to_string().as_bytes()),
        Tree::Number(n) => out.extend_from_slice(n.as_bytes()),
        Tree::Text(b) => quoted(b, escape, out),
        Tree::Array(items) => {
            out.push(b'[');
            out.extend_from_slice(pad.as_bytes());
            for (i, x) in items.iter().enumerate() {
                if i > 0 {
                    out.extend_from_slice(format!("{pad},{pad}").as_bytes());
                }
                tree_text(x, pad, escape, out);
            }
            out.extend_from_slice(pad.as_bytes());
            out.push(b']');
        }
        Tree::Object(members) => {
            out.push(b'{');
            out.extend_from_slice(pad.as_bytes());
            for (i, (k, x)) in members.iter().enumerate() {
                if i > 0 {
                    out.extend_from_slice(format!("{pad},{pad}").as_bytes());
                }
                quoted(k.as_bytes(), false, out);
                out.extend_from_slice(format!("{pad}:{pad}").as_bytes());
                tree_text(x, pad, escape, out);
            }
            out.extend_from_slice(pad.as_bytes());
            out.push(b'}');
        }
    }
}

/// Where a text stops being JSON, and the way to the value it stops in.
#[derive(Debug, PartialEq)]
struct Stop {
    at: usize,
    path: String,
}

/// A step of the way to a value: the name of a member or the place in an array.
enum Step {
    Key(String),
    Index(usize),
}

struct Model<'a> {
    b: &'a [u8],
    at: usize,
    /// The member of each open container that the reader is in.
    way: Vec<Step>,
}

impl Model<'_> {
    fn peek(&self) -> Option<u8> {
        self.b.get(self.at).copied()
    }

    /// Stops at `at`, in the member the reader is in.
    fn within<T>(&self, at: usize) -> Result<T, Stop> {
        Err(Stop { at, path: self.path(self.way.len()) })
    }

    fn path(&self, steps: usize) -> String {
        let mut out = String::new();
        for s in &self.way[..steps] {
            match s {
                Step::Key(k) => {
                    if !out.is_empty() {
                        out.push('.');
                    }
                    out.push_str(k);
                }
                Step::Index(i) => out.push_str(&format!("[{i}]")),
            }
        }
        out
    }

    fn space(&mut self) {
        while matches!(self.peek(), Some(b' ' | b'\t' | b'\n' | b'\r')) {
            self.at += 1;
        }
    }

    fn digits(&mut self) -> bool {
        let from = self.at;
        while matches!(self.peek(), Some(b'0'..=b'9')) {
            self.at += 1;
        }
        self.at > from
    }

    fn number(&mut self) -> Result<(), Stop> {
        if self.peek() == Some(b'-') {
            self.at += 1;
        }
        if self.peek() == Some(b'0') {
            self.at += 1;
        } else if !self.digits() {
            return self.within(self.at);
        }
        if self.peek() == Some(b'.') {
            self.at += 1;
            if !self.digits() {
                return self.within(self.at);
            }
        }
        if matches!(self.peek(), Some(b'e' | b'E')) {
            self.at += 1;
            if matches!(self.peek(), Some(b'+' | b'-')) {
                self.at += 1;
            }
            if !self.digits() {
                return self.within(self.at);
            }
        }
        Ok(())
    }

    fn hex4(&self, at: usize) -> Option<u32> {
        let digits = self.b.get(at..at + 4)?;
        if digits.iter().all(u8::is_ascii_hexdigit) {
            u32::from_str_radix(std::str::from_utf8(digits).ok()?, 16).ok()
        } else {
            None
        }
    }

    fn string(&mut self) -> Result<(), Stop> {
        self.at += 1;
        loop {
            match self.peek() {
                None | Some(0..=31) => return self.within(self.at),
                Some(b'"') => {
                    self.at += 1;
                    return Ok(());
                }
                Some(b'\\') => match self.b.get(self.at + 1).copied() {
                    Some(b'"' | b'\\' | b'/' | b'b' | b'f' | b'n' | b'r' | b't') => self.at += 2,
                    Some(b'u') => match self.hex4(self.at + 2) {
                        Some(0xd800..=0xdbff) => {
                            let low = self.b.get(self.at + 6..self.at + 8) == Some(&b"\\u"[..])
                                && matches!(self.hex4(self.at + 8), Some(0xdc00..=0xdfff));
                            if !low {
                                return self.within(self.at);
                            }
                            self.at += 12;
                        }
                        Some(0xdc00..=0xdfff) | None => return self.within(self.at),
                        Some(_) => self.at += 6,
                    },
                    _ => return self.within(self.at),
                },
                Some(_) => self.at += 1,
            }
        }
    }

    fn word(&mut self, word: &str) -> Result<(), Stop> {
        if self.b[self.at..].starts_with(word.as_bytes()) {
            self.at += word.len();
            Ok(())
        } else {
            self.within(self.at)
        }
    }

    fn value(&mut self) -> Result<(), Stop> {
        self.space();
        match self.peek() {
            Some(b'[') => {
                self.at += 1;
                self.space();
                if self.peek() == Some(b']') {
                    self.at += 1;
                    return Ok(());
                }
                let mut i = 0;
                loop {
                    self.way.push(Step::Index(i));
                    self.value()?;
                    self.way.pop();
                    self.space();
                    match self.peek() {
                        Some(b',') => {
                            self.at += 1;
                            i += 1;
                        }
                        Some(b']') => {
                            self.at += 1;
                            return Ok(());
                        }
                        _ => return self.within(self.at),
                    }
                }
            }
            Some(b'{') => {
                self.at += 1;
                self.space();
                if self.peek() == Some(b'}') {
                    self.at += 1;
                    return Ok(());
                }
                loop {
                    self.space();
                    if self.peek() != Some(b'"') {
                        return self.within(self.at);
                    }
                    let start = self.at;
                    self.string()?;
                    let key = String::from_utf8_lossy(&decoded(&self.b[start..self.at])).to_string();
                    self.way.push(Step::Key(key));
                    self.space();
                    if self.peek() != Some(b':') {
                        return self.within(self.at);
                    }
                    self.at += 1;
                    self.value()?;
                    self.way.pop();
                    self.space();
                    match self.peek() {
                        Some(b',') => self.at += 1,
                        Some(b'}') => {
                            self.at += 1;
                            return Ok(());
                        }
                        _ => return self.within(self.at),
                    }
                }
            }
            Some(b'"') => self.string(),
            Some(b'-' | b'0'..=b'9') => self.number(),
            Some(b't') => self.word("true"),
            Some(b'f') => self.word("false"),
            Some(b'n') => self.word("null"),
            // A bracket that closes what is not open at the value, which is
            // in the container and not in the member that was to come.
            Some(b']' | b'}') => {
                self.way.pop();
                self.within(self.at)
            }
            _ => self.within(self.at),
        }
    }
}

/// Where a text stops being JSON, or the size of the value it holds.
fn model(text: &[u8]) -> Result<usize, Stop> {
    let mut m = Model { b: text, at: 0, way: Vec::new() };
    m.space();
    let start = m.at;
    m.value()?;
    let end = m.at;
    m.space();
    if m.at < text.len() {
        return Err(Stop { at: m.at, path: String::new() });
    }
    Ok(end - start)
}

/// The bytes a string stands for, which a name is, for the way to a value.
fn decoded(quoted: &[u8]) -> Vec<u8> {
    let text = std::str::from_utf8(quoted).unwrap_or("\"\"");
    let mut out = String::new();
    let mut chars = text[1..text.len().saturating_sub(1)].chars();
    while let Some(c) = chars.next() {
        if c != '\\' {
            out.push(c);
            continue;
        }
        match chars.next() {
            Some('n') => out.push('\n'),
            Some('t') => out.push('\t'),
            Some('r') => out.push('\r'),
            Some('b') => out.push('\u{8}'),
            Some('f') => out.push('\u{c}'),
            Some('u') => {
                let hex: String = chars.by_ref().take(4).collect();
                out.push(char::from_u32(u32::from_str_radix(&hex, 16).unwrap_or(63)).unwrap_or('?'));
            }
            Some(other) => out.push(other),
            None => {}
        }
    }
    out.into_bytes()
}

/// A text that is changed in one place, as a byte taken out, put in, or put
/// for another, or the text cut short.
fn mutate(g: &mut Gen, text: &[u8]) -> Vec<u8> {
    const BYTES: &[u8] = b"[{]},:\"0\\-.et x\n";
    let at = g.below(text.len() + 1);
    let mut out = text.to_vec();
    match g.below(4) {
        0 if at < out.len() => {
            out.remove(at);
        }
        1 => out.insert(at, *g.pick(BYTES)),
        2 if at < out.len() => out[at] = *g.pick(BYTES),
        _ => out.truncate(at),
    }
    out
}

fn text_the_reader_of_the_test_refuses_is_refused_where_it_refuses_it() {
    generate::check(6005, 8, |g: &mut Gen| {
        let t = tree(g, 3);
        let pad = *g.pick(&["", " ", "\n", "\r\n\t "]);
        let escape = g.one_in(2);
        let mut text = pad.as_bytes().to_vec();
        tree_text(&t, pad, escape, &mut text);
        text.extend_from_slice(pad.as_bytes());
        let mut texts = vec![text.clone()];
        while texts.len() < 14 {
            let broken = mutate(g, &text);
            if std::str::from_utf8(&broken).is_ok() {
                texts.push(broken);
            }
        }
        texts
    }, |texts| {
        let mut source = format!(
            "use std.json (from_json, RawJson)\nuse std.deserialize (ReadError, Problem)\n\n{TEXT}fn main() ! console, alloc {{\n"
        );
        for (i, text) in texts.iter().enumerate() {
            source.push_str(&format!(
                "  r{i}: Result[RawJson, ReadError] = from_json({})\n  match r{i} {{\n    .ok(w) {{ print(\"ok {{w.text.size}}\") }}\n    .err(e) {{ print(say(e)) }}\n  }}\n",
                text_source(text)
            ));
        }
        source.push_str("}\n");
        let printed = lines(&prints(&source));
        assert_eq!(printed.len(), texts.len(), "{source}");
        for (line, text) in printed.iter().zip(texts) {
            let expected = match model(text) {
                Ok(size) => format!("ok {size}"),
                Err(stop) => error(text, stop.at, &stop.path, "malformed"),
            };
            assert_eq!(line, &expected, "{:?}", String::from_utf8_lossy(text));
        }
    });
}

// A part of the text that is not taken apart.

const WRAP: &str = "struct Wrap {
  head: u8
  body: RawJson
  tail: Option[str]
}

";

fn a_rawjson_holds_the_bytes_of_its_value_and_reads_later_to_the_value_a_direct_read_gives() {
    generate::check(6006, 6, |g: &mut Gen| {
        let (defs, t, v, settings) = made(g);
        let pad = *g.pick(&["", " ", "\n "]);
        (defs, t, v, settings, pad)
    }, |(defs, t, v, settings, pad)| {
        let style = Style { pad, escape: false, negative_zero: false };
        let written = write(defs, t, v, *settings, style);
        // The member's value is the text of the value, with the space around
        // it left out, and that is what is held.
        let body = written.bytes[pad.len()..written.bytes.len() - pad.len()].to_vec();
        let text = [&b"{\"head\":1,\"body\":"[..], &written.bytes, &b",\"tail\":null}"[..]].concat();
        let name = defs.name(t);
        let source = format!(
            "use std.json (from_json_as, to_json, Json, RawJson)\nuse std.deserialize (ReadError, Problem)\n\n{TEXT}{WRAP}{}fn main() ! console, alloc {{\n  v: {name} = {}\n  j = {}\n  r: Result[Wrap, ReadError] = from_json_as({}, j)\n  match r {{\n    .ok(w) {{\n      print(w.body.text)\n      later: Result[{name}, ReadError] = from_json_as(w.body.text, j)\n      match later {{\n        .ok(x) {{ print(\"{{to_json(x) == to_json(v)}}\") }}\n        .err(e) {{ print(say(e)) }}\n      }}\n    }}\n    .err(e) {{ print(say(e)) }}\n  }}\n}}\n",
            defs.source(),
            v.source(),
            settings.source(false),
            text_source(&text)
        );
        let printed = prints(&source);
        let expected = [&body[..], b"\ntrue\n"].concat();
        assert_eq!(String::from_utf8_lossy(&printed), String::from_utf8_lossy(&expected), "{source}");
    });
}

/// What a program prints, what it asks the system for, and what its lists move
/// into new memory as they grow, in bytes.
fn asked(source: &str) -> (Vec<u8>, usize, usize) {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{source}"));
    let (outcome, requests, copied) = scalar::run_copying(&program, usize::MAX);
    (outcome.stdout, requests.iter().sum(), copied.iter().sum())
}

/// A function that makes the text of a `Wrap`, whose body is `n` zeros in an
/// array, from the bytes of a text that is built with `push`.
const MAKE: &str = "fn make(n: Nat) -> str ! alloc {
  var l: List[u8] = []
  loop b in \"\\{\\\"head\\\":1,\\\"body\\\":[\" { l = push(l, b) }
  loop i in 0..n {
    if i > 0 { l = push(l, ',') }
    l = push(l, '0')
  }
  loop b in \"],\\\"tail\\\":null\\}\" { l = push(l, b) }
  s(l)
}

";

fn a_rawjson_shares_the_memory_of_the_input() {
    let program = |n: usize, read: bool| {
        let tail = if read {
            format!("  r: Result[Wrap, ReadError] = from_json(t)\n  match r {{\n    .ok(w) {{ print(if w.body.text.size == {} {{ \"ok\" }} else {{ \"wrong\" }}) }}\n    .err(e) {{ print(say(e)) }}\n  }}\n", 2 * n + 1)
        } else {
            "  print(\"ok\")\n".to_string()
        };
        format!(
            "use std.json (from_json, RawJson)\nuse std.deserialize (ReadError, Problem)\n\n{TEXT}{WRAP}{MAKE}fn main() ! console, alloc {{\n  t = make({n})\n{tail}}}\n"
        )
    };
    // What the reading asks for, beyond building the text, is the same for a
    // body of a hundred bytes and one of eight thousand.
    let cost = |n: usize| {
        let (stdout, with, copied) = asked(&program(n, true));
        let (_, without, copying) = asked(&program(n, false));
        assert_eq!(String::from_utf8(stdout).unwrap(), "ok\n");
        (with - without, copied - copying)
    };
    assert_eq!(cost(100), cost(1_000), "memory asked for by reading a body, and bytes copied");
}

/// Source that pushes the bytes of `text` on the list `l`.
fn pushes(text: &str) -> String {
    text.bytes().map(|b| format!("  l = push(l, {b})\n")).collect()
}

fn reading_asks_for_memory_for_the_texts_and_lists_of_the_value_and_for_nothing_else() {
    // What reading `ty` from the text that `build` pushes asks the system for,
    // beyond what building the text asks for, which is the same program that
    // does not read it.
    let extra = |decls: &str, ty: &str, build: &str, check: &str| {
        let program = |read: bool| {
            let tail = if read {
                format!(
                    "  r: Result[{ty}, ReadError] = from_json(t)\n  match r {{\n    .ok(w) {{ print(if {check} {{ \"ok\" }} else {{ \"wrong\" }}) }}\n    .err(e) {{ print(say(e)) }}\n  }}\n"
                )
            } else {
                "  print(\"ok\")\n".to_string()
            };
            format!(
                "use std.json (from_json)\nuse std.deserialize (ReadError, Problem)\n\n{TEXT}{decls}fn main() ! console, alloc {{\n  var l: List[u8] = []\n{build}  t = s(l)\n{tail}}}\n"
            )
        };
        let (stdout, with, copied) = asked(&program(true));
        let (_, without, copying) = asked(&program(false));
        assert_eq!(String::from_utf8(stdout).unwrap(), "ok\n", "{}", program(true));
        (with - without, copied - copying)
    };
    // A value that holds no text and no list asks for what a value of `()`
    // does, whatever it holds.
    let nothing = extra("", "()", &pushes("null"), "true");
    let few = extra("struct N {\n  a: u8\n}\n\n", "N", &pushes(r#"{"a":1}"#), "w.a == 1");
    let many = extra(
        "struct N {\n  a: u8\n  b: bool\n  c: Option[u8]\n  d: (u8, i8)\n  e: Mode\n  f: i64\n}\n\nenum Mode {\n  c1\n  c2: u8\n}\n\n",
        "N",
        &pushes(r#"{"a":1,"b":true,"c":null,"d":[7,-8],"e":"c1","f":-9223372036854775808}"#),
        "w.a == 1",
    );
    assert_eq!(few, nothing, "memory asked for a struct of one number");
    assert_eq!(many, nothing, "memory asked for a struct of numbers");

    // A list asks for memory in proportion to it, a text in proportion to what
    // it holds when it has an escape, and a text with none asks for nothing.
    let list = |n: usize| {
        let build = format!(
            "  l = push(l, '[')\n  loop i in 0..{n} {{\n    if i > 0 {{ l = push(l, ',') }}\n    l = push(l, '0')\n  }}\n  l = push(l, ']')\n"
        );
        extra("", "List[u64]", &build, &format!("w.size == {n}"))
    };
    let (moved_small, moved_large) = (list(500).1 - nothing.1, list(1_000).1 - nothing.1);
    // What the list moves as it grows doubles with it.
    assert!(moved_small > 0 && moved_small <= 64 * 500, "{moved_small} bytes moved for 500 elements");
    assert!(moved_large >= moved_small && moved_large <= 3 * moved_small, "{moved_large} bytes moved for 1000, {moved_small} for 500");
    let text = |escape: &str, n: usize| {
        let build = format!(
            "  l = push(l, '\"')\n  loop i in 0..{n} {{\n{escape}  }}\n  l = push(l, '\"')\n"
        );
        extra("", "str", &build, &format!("w.size == {n}"))
    };
    let plain = text("    l = push(l, 'a')\n", 1_000);
    assert_eq!(plain, nothing, "a text with no escape is a piece of the input");
    let escaped = text("    l = push(l, '\\\\')\n    l = push(l, 'n')\n", 1_000);
    let moved = escaped.1 - nothing.1;
    assert!(moved > 0 && moved <= 8 * 1_000, "{moved} bytes moved for 1000 escapes");
}

fn a_rawjson_that_is_read_and_written_is_the_bytes_it_was_less_the_space_round_it() {
    generate::check(6008, 8, |g: &mut Gen| {
        let mut texts = Vec::new();
        for _ in 0..6 {
            let value = tree(g, 3);
            let pad = *g.pick(&["", " ", "\n", "\r\n\t "]);
            let escape = g.one_in(2);
            let mut text = pad.as_bytes().to_vec();
            tree_text(&value, pad, escape, &mut text);
            text.extend_from_slice(pad.as_bytes());
            texts.push(text);
        }
        texts
    }, |texts| {
        let mut source = format!(
            "use std.json (from_json, to_json, RawJson)\nuse std.deserialize (ReadError, Problem)\n\n{TEXT}fn main() ! console, alloc {{\n"
        );
        for (i, text) in texts.iter().enumerate() {
            source.push_str(&format!(
                "  r{i}: Result[RawJson, ReadError] = from_json({})\n  match r{i} {{\n    .ok(w) {{ print(to_json(w)) }}\n    .err(e) {{ print(say(e)) }}\n  }}\n",
                text_source(text)
            ));
        }
        source.push_str("}\n");
        let printed = lines(&prints(&source));
        // A text can hold a line break between its tokens, which is a line of
        // its own in what is printed, so what is printed is compared whole.
        let expected: String = texts
            .iter()
            .map(|t| format!("{}\n", String::from_utf8(t.trim_ascii().to_vec()).unwrap()))
            .collect();
        assert_eq!(printed.join("\n") + "\n", expected, "{source}");
    });
}
