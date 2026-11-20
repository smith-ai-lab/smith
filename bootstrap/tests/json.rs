//! `std.json` writes a value that has `Serialize` as JSON text, with the
//! settings `knowledge/library/json.md` gives.
//!
//! The generator builds a type, to a depth of four, from numbers, `bool`,
//! `str`, `()`, `Option`, lists, tuples, structs, and enums, and a value of
//! it. The test holds a model that writes the value as text from the value
//! itself, and a reader of JSON, written here, that reads what the program
//! printed back into a tree, which is the tree the value makes.

use smith::{check, lower, scalar};

use crate::Test;
use crate::generate::{self, Gen};
use crate::lists::header;

pub const ONCE: &[Test] = &[
    (
        "every_example_of_the_page_gives_what_the_page_says",
        every_example_of_the_page_gives_what_the_page_says,
    ),
    (
        "every_value_is_written_as_the_model_says_for_each_setting_and_is_json_that_reads_back_as_its_tree_part_1_of_2",
        || every_value_is_written_as_the_model_says_for_each_setting_and_is_json_that_reads_back_as_its_tree(0),
    ),
    (
        "every_value_is_written_as_the_model_says_for_each_setting_and_is_json_that_reads_back_as_its_tree_part_2_of_2",
        || every_value_is_written_as_the_model_says_for_each_setting_and_is_json_that_reads_back_as_its_tree(1),
    ),
    (
        "a_text_is_written_so_that_the_reader_gives_the_same_bytes_back",
        a_text_is_written_so_that_the_reader_gives_the_same_bytes_back,
    ),
    (
        "the_least_and_the_greatest_of_each_whole_number_type_are_written_in_full",
        the_least_and_the_greatest_of_each_whole_number_type_are_written_in_full,
    ),
    (
        "camel_leaves_a_name_with_no_underscore_and_never_makes_two_snake_case_names_one",
        camel_leaves_a_name_with_no_underscore_and_never_makes_two_snake_case_names_one,
    ),
    (
        "a_case_with_a_tag_is_written_in_the_three_forms_of_the_table",
        a_case_with_a_tag_is_written_in_the_three_forms_of_the_table,
    ),
    (
        "a_tag_that_is_also_a_member_is_refused_wherever_it_is_met_and_nothing_else_is",
        a_tag_that_is_also_a_member_is_refused_wherever_it_is_met_and_nothing_else_is,
    ),
    (
        "a_rawjson_is_written_as_the_bytes_it_holds_and_under_value_when_a_case_holds_it",
        a_rawjson_is_written_as_the_bytes_it_holds_and_under_value_when_a_case_holds_it,
    ),
    (
        "a_program_that_does_not_import_std_json_is_the_program_it_was",
        a_program_that_does_not_import_std_json_is_the_program_it_was,
    ),
    (
        "to_json_asks_for_memory_in_proportion_to_the_text_it_gives",
        to_json_asks_for_memory_in_proportion_to_the_text_it_gives,
    ),
];

/// What a program prints, which is none of it when the program is refused.
fn prints(source: &str) -> Vec<u8> {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{source}"));
    scalar::run(&program).stdout
}

/// The function the programs print through.
const SHOW: &str = "fn show(r: Result[str, Clash]) ! console, alloc {\n  match r {\n    .ok(t) { print(t) }\n    .err(c) { print(\"err {c.of} {c.case} {c.member}\") }\n  }\n}\n\n";

/// The program of `rest`, which writes `show` and what follows it, opened with
/// the `use` lines it needs: `console` and `print`, the names of `std.json`
/// the programs use, what `rest` writes of the sizes, and `push` and `utf8`
/// where it calls them.
fn with_import(rest: String) -> String {
    let push: &[&str] = if rest.contains("push(") { &["push"] } else { &[] };
    let utf8: &[&str] = if rest.contains("utf8(") { &["utf8"] } else { &[] };
    let json: Vec<&str> = ["Clash", "Json", "to_json", "to_json_as"]
        .into_iter()
        .filter(|name| rest.contains(name) || SHOW.contains(name))
        .collect();
    let uses = header(
        &[
            ("std.console", &["console", "print"]),
            ("std.json", &json[..]),
            ("std.list", push),
            ("std.utf8", utf8),
        ],
        &rest,
    );
    format!("{uses}{SHOW}{rest}")
}

/// Source that turns bytes into a text, which a literal cannot do for a byte
/// below a space other than a line break and a tab.
const TEXT: &str = "fn s(bytes: Slice[Byte]) -> str {
  match utf8(bytes) {
    .ok(t) { t }
    .err(e) { \"\" }
  }
}

";

// A tree of JSON, and a reader of JSON text that gives one.

#[derive(Clone, Debug, PartialEq, Eq)]
enum Tree {
    Null,
    Bool(bool),
    Int(i128),
    Text(Vec<u8>),
    Array(Vec<Tree>),
    Object(Vec<(Vec<u8>, Tree)>),
}

struct Reader<'a> {
    bytes: &'a [u8],
    at: usize,
}

impl Reader<'_> {
    fn peek(&self) -> Option<u8> {
        self.bytes.get(self.at).copied()
    }

    fn expect(&mut self, b: u8) -> Result<(), String> {
        if self.peek() == Some(b) {
            self.at += 1;
            Ok(())
        } else {
            Err(format!("expected `{}` at {}", b as char, self.at))
        }
    }

    fn word(&mut self, word: &str, tree: Tree) -> Result<Tree, String> {
        if self.bytes[self.at..].starts_with(word.as_bytes()) {
            self.at += word.len();
            Ok(tree)
        } else {
            Err(format!("expected `{word}` at {}", self.at))
        }
    }

    fn value(&mut self) -> Result<Tree, String> {
        match self.peek() {
            Some(b'n') => self.word("null", Tree::Null),
            Some(b't') => self.word("true", Tree::Bool(true)),
            Some(b'f') => self.word("false", Tree::Bool(false)),
            Some(b'"') => Ok(Tree::Text(self.text()?)),
            Some(b'[') => {
                self.at += 1;
                let mut items = Vec::new();
                if self.peek() == Some(b']') {
                    self.at += 1;
                    return Ok(Tree::Array(items));
                }
                loop {
                    items.push(self.value()?);
                    match self.peek() {
                        Some(b',') => self.at += 1,
                        Some(b']') => {
                            self.at += 1;
                            return Ok(Tree::Array(items));
                        }
                        _ => return Err(format!("expected `,` or `]` at {}", self.at)),
                    }
                }
            }
            Some(b'{') => {
                self.at += 1;
                let mut members = Vec::new();
                if self.peek() == Some(b'}') {
                    self.at += 1;
                    return Ok(Tree::Object(members));
                }
                loop {
                    let key = self.text()?;
                    self.expect(b':')?;
                    members.push((key, self.value()?));
                    match self.peek() {
                        Some(b',') => self.at += 1,
                        Some(b'}') => {
                            self.at += 1;
                            return Ok(Tree::Object(members));
                        }
                        _ => return Err(format!("expected `,` or `}}` at {}", self.at)),
                    }
                }
            }
            Some(b'-' | b'0'..=b'9') => {
                let start = self.at;
                if self.peek() == Some(b'-') {
                    self.at += 1;
                }
                let digits = self.at;
                while self.peek().is_some_and(|b| b.is_ascii_digit()) {
                    self.at += 1;
                }
                let text = std::str::from_utf8(&self.bytes[start..self.at]).unwrap();
                if self.at == digits || (self.at - digits > 1 && self.bytes[digits] == b'0') {
                    return Err(format!("not a number: {text}"));
                }
                text.parse().map(Tree::Int).map_err(|e| format!("{text}: {e}"))
            }
            other => Err(format!("unexpected {other:?} at {}", self.at)),
        }
    }

    fn text(&mut self) -> Result<Vec<u8>, String> {
        self.expect(b'"')?;
        let mut out = Vec::new();
        loop {
            let b = self.peek().ok_or("a text that does not end")?;
            self.at += 1;
            match b {
                b'"' => break,
                b'\\' => {
                    let e = self.peek().ok_or("an escape that does not end")?;
                    self.at += 1;
                    match e {
                        b'"' | b'\\' | b'/' => out.push(e),
                        b'n' => out.push(b'\n'),
                        b't' => out.push(b'\t'),
                        b'r' => out.push(b'\r'),
                        b'b' => out.push(8),
                        b'f' => out.push(12),
                        b'u' => {
                            let hex = self.bytes.get(self.at..self.at + 4).ok_or("short \\u")?;
                            let hex = std::str::from_utf8(hex).map_err(|e| e.to_string())?;
                            let code = u32::from_str_radix(hex, 16).map_err(|e| e.to_string())?;
                            self.at += 4;
                            let c = char::from_u32(code).ok_or("a surrogate")?;
                            out.extend_from_slice(c.to_string().as_bytes());
                        }
                        other => return Err(format!("unknown escape {}", other as char)),
                    }
                }
                b if b < 0x20 => return Err(format!("a raw byte {b} in a text")),
                b => out.push(b),
            }
        }
        std::str::from_utf8(&out).map_err(|e| format!("not UTF-8: {e}"))?;
        Ok(out)
    }
}

/// The tree a text of JSON is, or why it is not JSON.
fn read(text: &[u8]) -> Result<Tree, String> {
    let mut reader = Reader { bytes: text, at: 0 };
    let tree = reader.value()?;
    if reader.at == text.len() {
        Ok(tree)
    } else {
        Err(format!("text after the value, at {}", reader.at))
    }
}

// The settings, and the model of the names.

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct Settings {
    /// The name of the tag, when a case is written with one.
    tag: Option<&'static str>,
    camel: bool,
}

const SETTINGS: [Settings; 7] = [
    Settings { tag: None, camel: false },
    Settings { tag: Some("type"), camel: false },
    Settings { tag: None, camel: true },
    Settings { tag: Some("type"), camel: true },
    // Tags that are the name of a field as it is written, and `value`.
    Settings { tag: Some("plain"), camel: false },
    Settings { tag: Some("aBC"), camel: true },
    Settings { tag: Some("value"), camel: false },
];

impl Settings {
    fn source(self) -> String {
        let cases = match self.tag {
            None => ".key".to_string(),
            Some(t) => format!(".tag(\"{t}\")"),
        };
        let names = if self.camel { ".camel" } else { ".written" };
        format!("Json(cases: {cases}, names: {names})")
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
    ("Int", i64::MIN as i128, i64::MAX as i128),
    ("Byte", 0, 255),
    ("u16", 0, 65535),
    ("u32", 0, 4294967295),
    ("u64", 0, u64::MAX as i128),
    ("Nat", 0, u64::MAX as i128),
];

/// The names of the fields of a struct, by their place: with and without
/// an underscore, so that a setting that changes names has names to change.
const FIELDS: [&str; 3] = ["first_name", "plain", "a_b_c"];

#[derive(Default)]
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

fn ty(g: &mut Gen, defs: &mut Defs, depth: usize) -> Ty {
    let kinds = if depth == 0 { 4 } else { 9 };
    match g.below(kinds) {
        0 => Ty::Bool,
        1 => Ty::Int(g.pick(INTS).0),
        2 => Ty::Text,
        3 => Ty::Unit,
        4 => Ty::Option(Box::new(ty(g, defs, depth - 1))),
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
                cases.push(held.then(|| ty(g, defs, depth - 1)));
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

/// The bytes of a text as the source of a `str`.
fn text_source(bytes: &[u8]) -> String {
    let list: Vec<String> = bytes.iter().map(|b| b.to_string()).collect();
    format!("s([{}])", list.join(", "))
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

    /// The tree the value makes under the settings.
    fn tree(&self, settings: Settings) -> Tree {
        let many = |of: &[Val]| of.iter().map(|v| v.tree(settings)).collect();
        match self {
            Val::Bool(b) => Tree::Bool(*b),
            Val::Int(n) => Tree::Int(*n),
            Val::Text(t) => Tree::Text(t.clone()),
            Val::Unit | Val::Option(None) => Tree::Null,
            Val::Option(Some(v)) => v.tree(settings),
            Val::List(of) | Val::Tuple(of) => Tree::Array(many(of)),
            Val::Struct(_, of) => Tree::Object(self.members(of, settings)),
            Val::Enum(_, c, held) => {
                let name = format!("c{c}").into_bytes();
                match (held, settings.tag) {
                    (None, None) => Tree::Text(name),
                    (Some(v), None) => Tree::Object(vec![(name, v.tree(settings))]),
                    (None, Some(tag)) => Tree::Object(vec![(tag.into(), Tree::Text(name))]),
                    (Some(v), Some(tag)) => {
                        let mut members = vec![(tag.into(), Tree::Text(name))];
                        match &**v {
                            Val::Struct(_, of) => members.extend(self.members(of, settings)),
                            other => members.push((b"value".to_vec(), other.tree(settings))),
                        }
                        Tree::Object(members)
                    }
                }
            }
        }
    }

    /// The first tag that is also a member, in the order of writing, as the
    /// enum, the case, and the name that a `Clash` holds.
    fn first_clash(&self, settings: Settings) -> Option<String> {
        let name = |f: usize| if settings.camel { camel(FIELDS[f]) } else { FIELDS[f].to_string() };
        match self {
            Val::Option(Some(v)) => v.first_clash(settings),
            Val::List(of) | Val::Tuple(of) => of.iter().find_map(|v| v.first_clash(settings)),
            Val::Struct(_, of) => of.iter().find_map(|v| v.first_clash(settings)),
            Val::Enum(e, c, Some(v)) => match (settings.tag, &**v) {
                (Some(tag), Val::Struct(_, of)) => of.iter().enumerate().find_map(|(f, v)| {
                    if name(f) == tag {
                        Some(format!("E{e} c{c} {tag}"))
                    } else {
                        v.first_clash(settings)
                    }
                }),
                (Some("value"), _) => Some(format!("E{e} c{c} value")),
                _ => v.first_clash(settings),
            },
            _ => None,
        }
    }

    fn members(&self, of: &[Val], settings: Settings) -> Vec<(Vec<u8>, Tree)> {
        of.iter()
            .enumerate()
            .map(|(f, v)| {
                let name = if settings.camel { camel(FIELDS[f]) } else { FIELDS[f].to_string() };
                (name.into_bytes(), v.tree(settings))
            })
            .collect()
    }
}

// The model of the text, which is written from the value as the page says.

/// A text as a string of JSON, with `"` and `\` escaped, a line break, a tab,
/// a return, a backspace, and a form feed as the letter after a `\`, and each
/// other byte below a space as `\u00` and two lower case hexadecimal digits.
fn quoted(bytes: &[u8], out: &mut Vec<u8>) {
    out.push(b'"');
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
    out.push(b'"');
}

/// The text of a tree, with no spaces and no line breaks.
fn written(tree: &Tree, out: &mut Vec<u8>) {
    match tree {
        Tree::Null => out.extend_from_slice(b"null"),
        Tree::Bool(b) => out.extend_from_slice(b.to_string().as_bytes()),
        Tree::Int(n) => out.extend_from_slice(n.to_string().as_bytes()),
        Tree::Text(t) => quoted(t, out),
        Tree::Array(of) => {
            out.push(b'[');
            for (i, v) in of.iter().enumerate() {
                if i > 0 {
                    out.push(b',');
                }
                written(v, out);
            }
            out.push(b']');
        }
        Tree::Object(members) => {
            out.push(b'{');
            for (i, (k, v)) in members.iter().enumerate() {
                if i > 0 {
                    out.push(b',');
                }
                quoted(k, out);
                out.push(b':');
                written(v, out);
            }
            out.push(b'}');
        }
    }
}

fn model(tree: &Tree) -> Vec<u8> {
    let mut out = Vec::new();
    written(tree, &mut out);
    out
}

/// A program that prints the JSON of the value under each setting, one line
/// for each.
fn program(defs: &Defs, t: &Ty, v: &Val) -> String {
    let mut out = format!("{TEXT}{}", defs.source());
    out.push_str(&format!("fn main() ! console, alloc {{\n  v: {} = {}\n", defs.name(t), v.source()));
    for settings in SETTINGS {
        out.push_str(&format!("  show(to_json_as(v, {}))\n", settings.source()));
    }
    out.push_str("  print(to_json(v))\n}\n");
    with_import(out)
}

/// The lines a program printed, as the bytes they hold.
fn lines(printed: &[u8]) -> Vec<&[u8]> {
    let mut lines: Vec<&[u8]> = printed.split(|b| *b == b'\n').collect();
    assert_eq!(lines.pop(), Some(&b""[..]), "the output ends with a line break");
    lines
}

// What the issue says, as it says it.

fn every_example_of_the_page_gives_what_the_page_says() {
    let source = with_import(String::from(
        "struct Point {
  x: Int
  y: Int
}

struct Circle {
  radius: u64
}

enum Shape {
  circle: Circle
  square: u64
  empty
}

struct Content {
  input_schema: Byte
}

fn main() ! console, alloc {
  print(to_json(Point(x: 3, y: 4)))
  print(to_json(Shape.empty))
  print(to_json(Shape.square(4)))
  print(to_json(Shape.circle(Circle(radius: 2))))
  nested: Option[Option[Byte]] = Option.some(Option.none)
  print(to_json(nested))
  mcp = Json(cases: .tag(\"type\"), names: .camel)
  show(to_json_as(Shape.empty, mcp))
  show(to_json_as(Shape.square(4), mcp))
  show(to_json_as(Shape.circle(Circle(radius: 2)), mcp))
  show(to_json_as(Content(input_schema: 1), mcp))
  show(to_json_as(Point(x: 3, y: 4), Json(cases: .tag(\"type\"), names: .camel)))
}
"
    ));
    let printed = prints(&source);
    let expected = [
        r#"{"x":3,"y":4}"#,
        r#""empty""#,
        r#"{"square":4}"#,
        r#"{"circle":{"radius":2}}"#,
        "null",
        r#"{"type":"empty"}"#,
        r#"{"type":"square","value":4}"#,
        r#"{"type":"circle","radius":2}"#,
        r#"{"inputSchema":1}"#,
        r#"{"x":3,"y":4}"#,
    ];
    let expected: String = expected.iter().map(|l| format!("{l}\n")).collect();
    assert_eq!(String::from_utf8(printed).unwrap(), expected);
}

fn every_value_is_written_as_the_model_says_for_each_setting_and_is_json_that_reads_back_as_its_tree(part: u64) {
    let generate = |g: &mut Gen| {
        let mut defs = Defs::default();
        let t = ty(g, &mut defs, 4);
        let v = value(g, &defs, &t);
        (program(&defs, &t, &v), v)
    };
    generate::check_part(5601, 15, part, 2, generate, |(source, v)| {
        let printed = prints(source);
        let lines = lines(&printed);
        let mut settings: Vec<Settings> = SETTINGS.to_vec();
        // `to_json` is the first setting.
        settings.push(SETTINGS[0]);
        assert_eq!(lines.len(), settings.len(), "{source}");
        for (line, settings) in lines.iter().zip(settings) {
            match v.first_clash(settings) {
                Some(clash) => assert_eq!(*line, format!("err {clash}").as_bytes(), "{settings:?}\n{source}"),
                None => {
                    let tree = v.tree(settings);
                    assert_eq!(*line, &model(&tree)[..], "{settings:?}\n{source}");
                    assert_eq!(read(line), Ok(tree), "{settings:?}\n{source}");
                }
            }
        }
    });
}

fn a_text_is_written_so_that_the_reader_gives_the_same_bytes_back() {
    // Every byte below a space, the bytes that are escaped, one byte of a
    // character, and characters of two, three, and four bytes.
    let pieces: Vec<Vec<u8>> = (0u8..32)
        .map(|b| vec![b])
        .chain([b'"', b'\\', b'/', 127].map(|b| vec![b]))
        .chain(["a", "é", "€", "😀", "\u{2028}"].map(|c| c.as_bytes().to_vec()))
        .collect();
    let generate = |g: &mut Gen| {
        let size = g.below(6);
        let mut bytes = Vec::new();
        for _ in 0..size {
            bytes.extend_from_slice(g.pick(&pieces));
        }
        bytes
    };
    // All of them in one text, so that the escape of each byte is read.
    let all: Vec<u8> = pieces.concat();
    let run = |bytes: &[u8]| {
        let source = with_import(format!(
            "{TEXT}fn main() ! console, alloc {{\n  print(to_json({}))\n}}\n",
            text_source(bytes)
        ));
        let printed = prints(&source);
        let lines = lines(&printed);
        assert_eq!(lines.len(), 1, "{source}");
        assert_eq!(read(lines[0]), Ok(Tree::Text(bytes.to_vec())), "{source}");
        assert_eq!(lines[0], &model(&Tree::Text(bytes.to_vec()))[..], "{source}");
    };
    run(&all);
    generate::check(5602, 6, generate, |bytes| run(bytes));
}

fn the_least_and_the_greatest_of_each_whole_number_type_are_written_in_full() {
    let mut body = String::new();
    for (name, least, greatest) in INTS {
        body.push_str(&format!(
            "  least_{name}: {name} = {least}\n  print(to_json(least_{name}))\n  greatest_{name}: {name} = {greatest}\n  print(to_json(greatest_{name}))\n"
        ));
    }
    let source = with_import(format!("fn main() ! console, alloc {{\n{body}}}\n"));
    let printed = prints(&source);
    let expected: String = INTS.iter().flat_map(|(_, l, g)| [l, g]).map(|n| format!("{n}\n")).collect();
    assert_eq!(String::from_utf8(printed).unwrap(), expected);
}

/// What `camel` writes for the names, from the program.
fn camel_of(names: &[&str]) -> Vec<String> {
    // A struct for each name, which has the name for its one field.
    let mut source = String::new();
    for (i, name) in names.iter().enumerate() {
        source.push_str(&format!("struct N{i} {{\n  {name}: Byte\n}}\n\n"));
    }
    source.push_str("fn main() ! console, alloc {\n  j = Json(cases: .key, names: .camel)\n");
    for (i, name) in names.iter().enumerate() {
        source.push_str(&format!("  show(to_json_as(N{i}({}: 1), j))\n", name));
    }
    source.push_str("}\n");
    let printed = prints(&with_import(source));
    lines(&printed)
        .iter()
        .map(|line| match read(line) {
            Ok(Tree::Object(m)) => String::from_utf8(m[0].0.clone()).unwrap(),
            other => panic!("{other:?} for {line:?}"),
        })
        .collect()
}

fn camel_leaves_a_name_with_no_underscore_and_never_makes_two_snake_case_names_one() {
    // The names the issue asks about, and ones with no underscore.
    let names = [
        "input_schema",
        "plain",
        "x1",
        "_x",
        "x_",
        "a__b",
        "a_1",
        "a_b_c",
        "__x",
        "a_b_",
        "_a_b",
        "a_1b",
        "z",
    ];
    let given = camel_of(&names);
    for (name, got) in names.iter().zip(&given) {
        assert_eq!(got, &camel(name), "{name}");
    }
    assert_eq!(given[0], "inputSchema");
    assert!(given[1..3].iter().zip(&names[1..3]).all(|(g, n)| g == n));
    // Two fields of one struct that differ are written with names that
    // differ, for every name of lower case letters, digits, and `_` of four
    // places or fewer, with each letter one of `a`, `b`, `1`, and `_`.
    let alphabet = ['a', '1', '_'];
    let mut all = Vec::new();
    for size in 1..=5 {
        let mut stack = vec![String::new()];
        while let Some(prefix) = stack.pop() {
            if prefix.len() == size {
                if prefix.as_bytes()[0] != b'1' {
                    all.push(prefix);
                }
                continue;
            }
            for c in alphabet {
                stack.push(format!("{prefix}{c}"));
            }
        }
    }
    let mut seen = std::collections::HashMap::new();
    for name in &all {
        if let Some(other) = seen.insert(camel(name), name) {
            panic!("`{other}` and `{name}` are both written `{}`", camel(name));
        }
    }
}

fn a_case_with_a_tag_is_written_in_the_three_forms_of_the_table() {
    let source = with_import(String::from(
        "struct Circle {
  radius: u64
}

enum Shape {
  empty
  circle: Circle
  square: u64
  pair: (Byte, Byte)
  maybe: Option[Circle]
  nested: Shape2
}

enum Shape2 {
  inner: Circle
  nothing
}

fn main() ! console, alloc {
  j = Json(cases: .tag(\"kind\"), names: .written)
  show(to_json_as(Shape.empty, j))
  show(to_json_as(Shape.circle(Circle(radius: 2)), j))
  show(to_json_as(Shape.square(4), j))
  show(to_json_as(Shape.pair((1, 2)), j))
  show(to_json_as(Shape.maybe(Option.some(Circle(radius: 5))), j))
  show(to_json_as(Shape.maybe(Option.none), j))
  show(to_json_as(Shape.nested(Shape2.inner(Circle(radius: 6))), j))
  show(to_json_as(Shape.nested(Shape2.nothing), j))
  shapes: List[Shape] = [Shape.empty, Shape.square(1)]
  show(to_json_as(shapes, j))
}
"
    ));
    let printed = String::from_utf8(prints(&source)).unwrap();
    let expected = [
        r#"{"kind":"empty"}"#,
        r#"{"kind":"circle","radius":2}"#,
        r#"{"kind":"square","value":4}"#,
        r#"{"kind":"pair","value":[1,2]}"#,
        r#"{"kind":"maybe","value":{"radius":5}}"#,
        r#"{"kind":"maybe","value":null}"#,
        r#"{"kind":"nested","value":{"kind":"inner","radius":6}}"#,
        r#"{"kind":"nested","value":{"kind":"nothing"}}"#,
        r#"[{"kind":"empty"},{"kind":"square","value":1}]"#,
    ];
    let expected: String = expected.iter().map(|l| format!("{l}\n")).collect();
    assert_eq!(printed, expected);
}

fn a_tag_that_is_also_a_member_is_refused_wherever_it_is_met_and_nothing_else_is() {
    let source = with_import(String::from(
        "struct Circle {
  type: str
  radius: u64
}

struct Plain {
  radius: u64
}

struct Named {
  my_tag: Byte
}

struct Holder {
  inside: Shape
}

enum Shape {
  circle: Circle
  plain: Plain
  square: u64
  empty
  inner: Wrap
}

enum Wrap {
  circle: Circle
}

fn main() ! console, alloc {
  t = Json(cases: .tag(\"type\"), names: .written)
  v = Json(cases: .tag(\"value\"), names: .written)
  c = Circle(type: \"round\", radius: 2)
  show(to_json_as(Shape.circle(c), t))
  show(to_json_as(Shape.plain(Plain(radius: 1)), t))
  show(to_json_as(c, t))
  show(to_json_as(Shape.inner(Wrap.circle(c)), t))
  l1: List[Shape] = [Shape.empty, Shape.circle(c), Shape.square(1)]
  show(to_json_as(l1, t))
  show(to_json_as(Holder(inside: Shape.circle(c)), t))
  show(to_json_as(Shape.square(4), v))
  show(to_json_as(Shape.empty, v))
  show(to_json_as(Shape.plain(Plain(radius: 1)), v))
  l2: List[Shape] = [Shape.square(1), Shape.circle(c)]
  show(to_json_as(l2, t))
  l3: List[Shape] = [Shape.circle(c), Shape.square(1)]
  show(to_json_as(l3, v))
  show(to_json_as(Named(my_tag: 1), Json(cases: .tag(\"myTag\"), names: .camel)))
  show(to_json_as(Holder2.of(Named(my_tag: 1)), Json(cases: .tag(\"myTag\"), names: .camel)))
  show(to_json_as(Holder2.of(Named(my_tag: 1)), Json(cases: .tag(\"my_tag\"), names: .camel)))
  print(to_json(Shape.circle(c)))
}

enum Holder2 {
  of: Named
}
"
    ));
    let printed = String::from_utf8(prints(&source)).unwrap();
    let expected = [
        "err Shape circle type",
        r#"{"type":"plain","radius":1}"#,
        r#"{"type":"round","radius":2}"#,
        "err Wrap circle type",
        "err Shape circle type",
        "err Shape circle type",
        "err Shape square value",
        r#"{"value":"empty"}"#,
        r#"{"value":"plain","radius":1}"#,
        "err Shape circle type",
        "err Shape square value",
        r#"{"myTag":1}"#,
        "err Holder2 of myTag",
        r#"{"my_tag":"of","myTag":1}"#,
        r#"{"circle":{"type":"round","radius":2}}"#,
    ];
    let expected: String = expected.iter().map(|l| format!("{l}\n")).collect();
    assert_eq!(printed, expected);
}

// Where `std.json` is, and what it costs.

fn a_program_that_does_not_import_std_json_is_the_program_it_was() {
    let items = "struct Point {\n  x: Int\n  y: Int\n}\n\nfn main() ! console, alloc {\n  p = Point(x: 3, y: 4)\n  print(\"{p.x} {p.y}\")\n}\n";
    let base = format!("use std.console (console, print)\n\n{items}");
    // The same program, with the module imported and a function that writes
    // a value that nothing calls.
    let extended = with_import(format!(
        "fn unused(p: Point) -> str ! alloc {{ to_json(p) }}\n\n{items}"
    ));
    let (a, b) = (lower(base.as_bytes()).unwrap(), lower(extended.as_bytes()).unwrap());
    assert_eq!(a, b);
    // Nothing of JSON is in a program that writes a value but not as JSON.
    let source = with_import(items.to_string());
    assert!(check(source.as_bytes()).is_ok());
}

fn to_json_asks_for_memory_in_proportion_to_the_text_it_gives() {
    // A list of `n` small structs, and the same list built and not written,
    // so that what the writing asks for is the difference of the two.
    let requests = |n: usize, write: bool| {
        let tail = if write {
            "  t = to_json(xs)\n  print(\"{t.size}\")\n"
        } else {
            "  print(\"{xs.size}\")\n"
        };
        let source = with_import(format!(
            "struct P {{\n  a: Byte\n  b: bool\n}}\n\nfn main() ! console, alloc {{\n  var xs: List[P] = []\n  loop i in 0..{n} {{ xs = push(xs, P(a: 7, b: true)) }}\n{tail}}}\n"
        ));
        let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{source}"));
        let (outcome, requests) = scalar::run_counting(&program, usize::MAX);
        (outcome.stdout, requests)
    };
    // One struct gives `{"a":7,"b":true}`, 16 bytes, and a comma between.
    for n in [10usize, 1_000, 2_000] {
        let (stdout, with) = requests(n, true);
        let (_, without) = requests(n, false);
        let text = n * 16 + (n - 1) + 2;
        assert_eq!(String::from_utf8(stdout).unwrap(), format!("{text}\n"));
        let asked: usize = with.iter().sum::<usize>() - without.iter().sum::<usize>();
        // The text and its list of bytes grow by doubling, and the writing
        // asks for memory for nothing else.
        assert!(asked <= 8 * text, "{asked} bytes asked for {text} bytes, {n} elements");
        assert!(with.len() - without.len() <= 2 * (text.ilog2() as usize + 2), "{} requests, {n} elements", with.len() - without.len());
    }
}

fn a_rawjson_is_written_as_the_bytes_it_holds_and_under_value_when_a_case_holds_it() {
    // The bytes of each are kept, with the space in them and every digit of the
    // numbers.
    let texts = [
        "1.50",
        "1e400",
        "1234567890123456789012345678901234567890",
        "-0.0",
        "[ 1 , {\"a\" : null} ]",
        "\"a\\u00e9\\/b\"",
    ];
    let mut source = String::from(
        "use std.console (console, print)\nuse std.json (Clash, Json, RawJson, raw_json, to_json, to_json_as)\nuse std.utf8 (utf8)\n\n\
         struct Call {\n  args: RawJson\n}\n\n\
         enum Reply {\n  result: RawJson\n  nothing\n}\n\n",
    );
    source.push_str(TEXT);
    source.push_str(
        "fn show(r: Result[str, Clash]) ! console, alloc {\n  match r {\n    .ok(t) { print(t) }\n    .err(c) { print(\"err {c.of} {c.case} {c.member}\") }\n  }\n}\n\nfn main() ! console, alloc {\n",
    );
    for text in texts {
        source.push_str(&format!(
            "  match raw_json({}) {{\n    .ok(r) {{ print(to_json(Call(args: r))) }}\n    .err(e) {{ print(\"err\") }}\n  }}\n",
            text_source(text.as_bytes())
        ));
    }
    source.push_str(
        "  match raw_json(\"7\") {\n    .ok(seven) {\n      r = Reply.result(seven)\n      show(to_json_as(r, Json(cases: .key, names: .written)))\n      show(to_json_as(r, Json(cases: .tag(\"type\"), names: .written)))\n      show(to_json_as(r, Json(cases: .tag(\"value\"), names: .written)))\n    }\n    .err(e) { print(\"err\") }\n  }\n}\n",
    );
    let printed = String::from_utf8(prints(&source)).unwrap();
    let mut expected: Vec<String> = texts.iter().map(|t| format!("{{\"args\":{t}}}")).collect();
    expected.push("{\"result\":7}".to_string());
    expected.push("{\"type\":\"result\",\"value\":7}".to_string());
    expected.push("err Reply result value".to_string());
    assert_eq!(printed.lines().collect::<Vec<_>>(), expected, "{source}");
}
