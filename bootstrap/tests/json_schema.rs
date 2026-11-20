//! The describer of JSON Schema in `std.jsonschema` gives the draft 2020-12 schema of
//! a type, which the Model Context Protocol takes as its default dialect, for
//! the JSON that `to_json_as` writes and `from_json_as` reads with the same
//! settings: every value of a generated type, written as JSON, fits the schema
//! of its type, and a field is left out of `required` exactly where
//! `from_json` reads a missing one. A small checker of schemas, which knows
//! the keywords the describer writes and stops at any other, decides whether a
//! text fits.

use smith::{lower, scalar};

use crate::Test;
use crate::generate::{self, Gen};
use crate::serialize::{Defs, Parts, ty, value, with_import};
use crate::targets;

pub const ONCE: &[Test] = &[
    (
        "the_checker_of_schemas_refuses_what_a_schema_does_not_allow",
        the_checker_of_schemas_refuses_what_a_schema_does_not_allow,
    ),
    (
        "a_schema_names_every_field_and_case_with_its_comment",
        a_schema_names_every_field_and_case_with_its_comment,
    ),
    (
        "every_value_written_as_json_fits_the_schema_of_its_type_part_1_of_2",
        || every_value_written_as_json_fits_the_schema_of_its_type(0),
    ),
    (
        "every_value_written_as_json_fits_the_schema_of_its_type_part_2_of_2",
        || every_value_written_as_json_fits_the_schema_of_its_type(1),
    ),
    (
        "a_field_is_left_out_of_required_exactly_where_from_json_reads_it_missing_part_1_of_3",
        || a_field_is_left_out_of_required_exactly_where_from_json_reads_it_missing(0),
    ),
    (
        "a_field_is_left_out_of_required_exactly_where_from_json_reads_it_missing_part_2_of_3",
        || a_field_is_left_out_of_required_exactly_where_from_json_reads_it_missing(1),
    ),
    (
        "a_field_is_left_out_of_required_exactly_where_from_json_reads_it_missing_part_3_of_3",
        || a_field_is_left_out_of_required_exactly_where_from_json_reads_it_missing(2),
    ),
];

/// The executables of the programs are checked against the evaluator.
pub const MACHINE: &[Test] = &[(
    "every_executable_of_a_schema_prints_what_the_evaluator_says",
    every_executable_of_a_schema_prints_what_the_evaluator_says,
)];

// JSON, and the checker.

#[derive(Debug, Clone, PartialEq)]
enum Json {
    Null,
    Bool(bool),
    Int(i128),
    Str(String),
    Arr(Vec<Json>),
    Obj(Vec<(String, Json)>),
}

struct Parser<'a> {
    bytes: &'a [u8],
    at: usize,
}

impl Parser<'_> {
    fn space(&mut self) {
        while self.at < self.bytes.len() && self.bytes[self.at].is_ascii_whitespace() {
            self.at += 1;
        }
    }

    fn word(&mut self, w: &str) {
        assert!(self.bytes[self.at..].starts_with(w.as_bytes()), "at {}", self.at);
        self.at += w.len();
    }

    fn string(&mut self) -> String {
        assert_eq!(self.bytes[self.at], b'"');
        self.at += 1;
        let mut out = Vec::new();
        loop {
            let b = self.bytes[self.at];
            self.at += 1;
            match b {
                b'"' => break,
                b'\\' => {
                    let e = self.bytes[self.at];
                    self.at += 1;
                    out.push(match e {
                        b'n' => b'\n',
                        b't' => b'\t',
                        b'r' => b'\r',
                        b'b' => 8,
                        b'f' => 12,
                        b'u' => {
                            let hex = std::str::from_utf8(&self.bytes[self.at..self.at + 4]).unwrap();
                            self.at += 4;
                            u8::try_from(u32::from_str_radix(hex, 16).unwrap()).expect("ascii")
                        }
                        other => other,
                    });
                }
                _ => out.push(b),
            }
        }
        String::from_utf8(out).expect("text")
    }

    fn value(&mut self) -> Json {
        self.space();
        let b = self.bytes[self.at];
        match b {
            b'n' => {
                self.word("null");
                Json::Null
            }
            b't' => {
                self.word("true");
                Json::Bool(true)
            }
            b'f' => {
                self.word("false");
                Json::Bool(false)
            }
            b'"' => Json::Str(self.string()),
            b'[' => {
                self.at += 1;
                let mut items = Vec::new();
                self.space();
                if self.bytes[self.at] == b']' {
                    self.at += 1;
                    return Json::Arr(items);
                }
                loop {
                    items.push(self.value());
                    self.space();
                    let c = self.bytes[self.at];
                    self.at += 1;
                    match c {
                        b',' => {}
                        b']' => break,
                        _ => panic!("array at {}", self.at),
                    }
                }
                Json::Arr(items)
            }
            b'{' => {
                self.at += 1;
                let mut members = Vec::new();
                self.space();
                if self.bytes[self.at] == b'}' {
                    self.at += 1;
                    return Json::Obj(members);
                }
                loop {
                    self.space();
                    let name = self.string();
                    self.space();
                    assert_eq!(self.bytes[self.at], b':');
                    self.at += 1;
                    members.push((name, self.value()));
                    self.space();
                    let c = self.bytes[self.at];
                    self.at += 1;
                    match c {
                        b',' => {}
                        b'}' => break,
                        _ => panic!("object at {}", self.at),
                    }
                }
                Json::Obj(members)
            }
            _ => {
                let from = self.at;
                while self.at < self.bytes.len()
                    && (self.bytes[self.at] == b'-' || self.bytes[self.at].is_ascii_digit())
                {
                    self.at += 1;
                }
                let text = std::str::from_utf8(&self.bytes[from..self.at]).unwrap();
                Json::Int(text.parse().unwrap_or_else(|_| panic!("number `{text}`")))
            }
        }
    }
}

fn parse(text: &str) -> Json {
    let mut p = Parser {
        bytes: text.as_bytes(),
        at: 0,
    };
    let v = p.value();
    p.space();
    assert_eq!(p.at, text.len(), "after the value of `{text}`");
    v
}

fn member<'a>(of: &'a [(String, Json)], name: &str) -> Option<&'a Json> {
    of.iter().find(|(n, _)| n == name).map(|(_, v)| v)
}

/// Whether the value fits the schema, for the keywords the describer writes.
fn fits(schema: &Json, value: &Json) -> bool {
    let Json::Obj(keywords) = schema else {
        panic!("a schema is an object: {schema:?}");
    };
    keywords.iter().all(|(keyword, argument)| match keyword.as_str() {
        "description" => true,
        "type" => {
            let Json::Str(t) = argument else { panic!("type") };
            match (t.as_str(), value) {
                ("boolean", Json::Bool(_))
                | ("integer", Json::Int(_))
                | ("string", Json::Str(_))
                | ("null", Json::Null)
                | ("array", Json::Arr(_))
                | ("object", Json::Obj(_)) => true,
                ("boolean" | "integer" | "string" | "null" | "array" | "object", _) => false,
                _ => panic!("type {t}"),
            }
        }
        "minimum" | "maximum" => {
            let (Json::Int(limit), Json::Int(n)) = (argument, value) else {
                return !matches!(value, Json::Int(_));
            };
            if keyword == "minimum" { n >= limit } else { n <= limit }
        }
        "const" => argument == value,
        "items" => match value {
            Json::Arr(items) => items.iter().all(|i| fits(argument, i)),
            _ => true,
        },
        "prefixItems" => match (argument, value) {
            (Json::Arr(each), Json::Arr(items)) => (each.iter().zip(items)).all(|(s, i)| fits(s, i)),
            _ => true,
        },
        "minItems" | "maxItems" => match (argument, value) {
            (Json::Int(n), Json::Arr(items)) => {
                if keyword == "minItems" {
                    items.len() as i128 >= *n
                } else {
                    items.len() as i128 <= *n
                }
            }
            _ => true,
        },
        "properties" => match (argument, value) {
            (Json::Obj(each), Json::Obj(members)) => members
                .iter()
                .all(|(n, v)| member(each, n).is_none_or(|s| fits(s, v))),
            _ => true,
        },
        "required" => match (argument, value) {
            (Json::Arr(names), Json::Obj(members)) => names.iter().all(|n| {
                let Json::Str(n) = n else { panic!("required") };
                member(members, n).is_some()
            }),
            _ => true,
        },
        "additionalProperties" => {
            assert_eq!(argument, &Json::Bool(false), "only `false` is written");
            let Json::Obj(members) = value else { return true };
            let known = member(keywords, "properties");
            let Some(Json::Obj(known)) = known else { return members.is_empty() };
            members.iter().all(|(n, _)| member(known, n).is_some())
        }
        "anyOf" | "oneOf" => {
            let Json::Arr(each) = argument else { panic!("{keyword}") };
            let n = each.iter().filter(|s| fits(s, value)).count();
            if keyword == "anyOf" { n >= 1 } else { n == 1 }
        }
        other => panic!("the checker does not know `{other}`"),
    })
}

fn the_checker_of_schemas_refuses_what_a_schema_does_not_allow() {
    let schema = parse(
        r#"{"type":"object","properties":{"a":{"type":"integer","minimum":1,"maximum":5},"b":{"anyOf":[{"type":"string"},{"type":"null"}]}},"required":["a"],"additionalProperties":false}"#,
    );
    for good in [r#"{"a":1}"#, r#"{"a":5,"b":null}"#, r#"{"b":"x","a":3}"#] {
        assert!(fits(&schema, &parse(good)), "{good}");
    }
    for bad in [
        r#"{}"#,
        r#"{"a":0}"#,
        r#"{"a":6}"#,
        r#"{"a":"1"}"#,
        r#"{"a":1,"b":2}"#,
        r#"{"a":1,"c":1}"#,
        r#"[1]"#,
    ] {
        assert!(!fits(&schema, &parse(bad)), "{bad}");
    }
    let one = parse(r#"{"oneOf":[{"type":"integer"},{"type":"integer","minimum":0}]}"#);
    assert!(fits(&one, &parse("-1")));
    assert!(!fits(&one, &parse("1")), "two alternatives fit");
    let pair = parse(r#"{"type":"array","prefixItems":[{"type":"integer"},{"type":"string"}],"minItems":2,"maxItems":2}"#);
    assert!(fits(&pair, &parse(r#"[1,"a"]"#)));
    assert!(!fits(&pair, &parse(r#"[1]"#)));
    assert!(!fits(&pair, &parse(r#"["a",1]"#)));
}

// Programs.

fn imports() -> Vec<(&'static str, &'static [&'static str])> {
    vec![
        ("std.describe", &["Describe"]),
        ("std.json", &["Json", "to_json_as"]),
        ("std.jsonschema", &["json_schema", "schema_text"]),
    ]
}

fn printed(source: &str) -> String {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{source}"));
    String::from_utf8(scalar::run(&program).stdout).expect("text")
}

/// The settings a test writes and reads with, as source.
const SETTINGS: &[&str] = &[
    "Json(cases: .key, names: .written)",
    "Json(cases: .tag(\"type\"), names: .camel)",
    "Json(cases: .key, names: .camel, unknown: .refuse)",
    "Json(cases: .tag(\"kind\"), names: .written, unknown: .refuse)",
];

// What the comments say, as the text.

fn a_schema_names_every_field_and_case_with_its_comment() {
    let rest = |settings: &str| {
        format!(
            "// What the note is.\nstruct Note {{\n  // What the note is called.\n  title: str\n  // How much it matters.\n  weight: 1..6 = 3\n  tags: List[str]\n  kind: Kind\n  extra_note: Option[bool]\n}}\n\n// A thing.\nenum Kind {{\n  // Plain.\n  plain\n  fancy: str\n}}\n\nfn main() ! console, alloc {{\n  print(schema_text(Describe[Note].describe(json_schema({settings}))))\n}}\n"
        )
    };
    let said = |settings: &str| printed(&with_import(&imports(), &rest(settings)));
    assert_eq!(
        said(SETTINGS[0]).trim_end(),
        r#"{"description":"What the note is.","type":"object","properties":{"title":{"description":"What the note is called.","type":"string"},"weight":{"description":"How much it matters.","type":"integer","minimum":1,"maximum":5},"tags":{"type":"array","items":{"type":"string"}},"kind":{"description":"A thing.","oneOf":[{"description":"Plain.","const":"plain"},{"type":"object","properties":{"fancy":{"type":"string"}},"required":["fancy"]}]},"extra_note":{"anyOf":[{"type":"boolean"},{"type":"null"}]}},"required":["title","tags","kind"]}"#
    );
    // A tag, camel names, and a member that is refused.
    let tagged = said(SETTINGS[1]);
    assert!(tagged.contains(r#""extraNote":{"anyOf""#), "{tagged}");
    assert!(
        tagged.contains(r#"{"type":"object","properties":{"type":{"const":"fancy"},"value":{"type":"string"}},"required":["type","value"]}"#),
        "{tagged}"
    );
    assert!(
        tagged.contains(r#"{"description":"Plain.","type":"object","properties":{"type":{"const":"plain"}},"required":["type"]}"#),
        "{tagged}"
    );
    let refused = said(SETTINGS[2]);
    let refused = refused.trim_end();
    assert!(refused.ends_with(r#""required":["title","tags","kind"],"additionalProperties":false}"#), "{refused}");
}

// A value of every generated type fits its schema.

#[derive(Debug)]
struct Case {
    source: String,
}

/// A program that prints the schema of a generated type and then a generated
/// value of it written as JSON, with the same settings.
fn case(g: &mut Gen) -> Case {
    let parts = Parts {
        lists: true,
        own: false,
        barred: false,
    };
    let mut defs = Defs::default();
    let top = ty(g, &mut defs, 3, parts);
    let (value, _) = value(g, &defs, &top);
    let settings = *g.pick(SETTINGS);
    // Fields are written with an underscore, which `Names.camel` takes out.
    let defs_source = rename(&defs.source());
    let rest = format!(
        "{defs_source}fn main() ! console, alloc {{\n  v: {} = {}\n  print(schema_text(Describe[{}].describe(json_schema({settings}))))\n  match to_json_as(v, {settings}) {{\n    .ok(t) {{ print(t) }}\n    .err(c) {{ print(\"clash\") }}\n  }}\n}}\n",
        defs.name(&top),
        rename(&value),
        defs.name(&top),
    );
    Case {
        source: with_import(&imports(), &rest),
    }
}

/// Each field `f3` is written `a_b3`.
fn rename(text: &str) -> String {
    let mut out = String::new();
    let bytes: Vec<char> = text.chars().collect();
    let mut i = 0;
    while i < bytes.len() {
        let word = i == 0 || !(bytes[i - 1].is_alphanumeric() || bytes[i - 1] == '_');
        if word && bytes[i] == 'f' && bytes.get(i + 1).is_some_and(|c| c.is_ascii_digit()) {
            out.push_str("a_b");
        } else {
            out.push(bytes[i]);
        }
        i += 1;
    }
    out
}

fn every_value_written_as_json_fits_the_schema_of_its_type(part: u64) {
    generate::check_part(6131, 24, part, 2, case, |c| {
        let said = printed(&c.source);
        let (schema, value) = said.split_once('\n').expect("two lines");
        let value = value.trim_end();
        assert_ne!(value, "clash", "{}", c.source);
        assert!(
            fits(&parse(schema), &parse(value)),
            "the value fits no schema\n{schema}\n{value}\n{}",
            c.source
        );
    });
}

// A field left out.

/// A struct of fields that each have a type that a document is made of, a
/// default, or neither, and every document that has some of the fields.
#[derive(Debug)]
struct Fields {
    /// The type of each field, and whether it has a default.
    each: Vec<(&'static str, bool)>,
    settings: &'static str,
}

fn fields(g: &mut Gen) -> Fields {
    let count = 1 + g.below(3);
    let each = (0..count)
        .map(|_| {
            let t = *g.pick(&["Int", "bool", "str", "Option[Int]"]);
            (t, t != "Option[Int]" && g.one_in(2))
        })
        .collect();
    Fields {
        each,
        settings: g.pick(SETTINGS),
    }
}

fn escape(text: &str) -> String {
    text.replace('\\', "\\\\")
        .replace('"', "\\\"")
        .replace('{', "\\{")
        .replace('}', "\\}")
}

fn a_field_is_left_out_of_required_exactly_where_from_json_reads_it_missing(part: u64) {
    generate::check_part(6132, 12, part, 3, fields, |f| {
        let mut source = String::from("struct S {\n");
        for (k, (t, default)) in f.each.iter().enumerate() {
            let tail = match (*t, default) {
                (_, false) => "",
                ("Int", _) => " = 0",
                ("bool", _) => " = false",
                _ => " = \"\"",
            };
            source += &format!("  f{k}: {t}{tail}\n");
        }
        source += "}\n\n";
        let source = rename(&source);
        let name = |k: usize| format!("a_b{k}");
        // The documents: each subset of the fields, each as small as it can be.
        let mut docs = Vec::new();
        for subset in 0..(1usize << f.each.len()) {
            let members: Vec<String> = (f.each.iter().enumerate())
                .filter(|(k, _)| subset >> k & 1 == 1)
                .map(|(k, (t, _))| {
                    let v = match *t {
                        "Int" => "1",
                        "bool" => "true",
                        "str" => "\"a\"",
                        _ => "null",
                    };
                    format!("\"{}\":{v}", written(f.settings, &name(k)))
                })
                .collect();
            docs.push(format!("{{{}}}", members.join(",")));
        }
        let mut main = String::from("fn check(text: str) -> str ! alloc {\n  r: Result[S, ReadError] = from_json_as(text, ");
        main += f.settings;
        main += ")\n  match r {\n    .ok(v) { \"ok\" }\n    .err(e) { \"err\" }\n  }\n}\n\nfn main() ! console, alloc {\n";
        main += &format!(
            "  print(schema_text(Describe[S].describe(json_schema({}))))\n",
            f.settings
        );
        for d in &docs {
            main += &format!("  print(check(\"{}\"))\n", escape(d));
        }
        main += "}\n";
        let program = with_import(
            &[
                ("std.describe", &["Describe"][..]),
                ("std.json", &["Json", "from_json_as"][..]),
                ("std.jsonschema", &["json_schema", "schema_text"][..]),
                ("std.deserialize", &["ReadError"][..]),
            ],
            &format!("{source}{main}"),
        );
        let said = printed(&program);
        let mut lines = said.lines();
        let schema = parse(lines.next().expect("the schema"));
        for doc in &docs {
            let read = lines.next().expect("a result") == "ok";
            assert_eq!(
                fits(&schema, &parse(doc)),
                read,
                "for {doc}\n{said}\n{program}"
            );
        }
    });
}

/// The name a field is written as, which is `Names.camel`'s when the settings
/// say so: `a_b3` is `aB3`.
fn written(settings: &str, name: &str) -> String {
    if settings.contains("camel") {
        name.replacen("a_b", "aB", 1)
    } else {
        name.to_string()
    }
}

fn every_executable_of_a_schema_prints_what_the_evaluator_says() {
    generate::check(6133, 3, case, |c| {
        let program = lower(c.source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{}", c.source));
        let said = scalar::run(&program);
        for target in targets::runnable() {
            targets::runs_as(c.source.as_bytes(), target, &said);
        }
    });
}
