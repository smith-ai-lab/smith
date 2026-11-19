//! A program that reads a value and writes a value builds, whichever way it
//! lists what it uses of `std.deserialize` and `std.json`, and what it reads
//! it writes as it was written.
//!
//! The generator of `serialize.rs` builds the type and the value. The program
//! writes the value, reads the text, and writes what it read, and the two
//! texts are the same.

use smith::{check, library_text, lower, scalar};

use crate::Test;
use crate::generate::{self, Gen};
use crate::programs::pos_after;
use crate::serialize::{self, Defs, Parts};

pub const ONCE: &[Test] = &[
    (
        "the_program_that_reads_and_writes_a_struct_prints_the_text_it_read",
        the_program_that_reads_and_writes_a_struct_prints_the_text_it_read,
    ),
    (
        "a_value_written_read_and_written_again_is_the_same_text_each_time_whatever_the_program_lists",
        a_value_written_read_and_written_again_is_the_same_text_each_time_whatever_the_program_lists,
    ),
    (
        "a_program_that_defines_the_names_of_the_library_beside_the_library_listed_as_other_names_does_what_each_says",
        a_program_that_defines_the_names_of_the_library_beside_the_library_listed_as_other_names_does_what_each_says,
    ),
    (
        "a_program_that_does_not_list_read_error_is_one_error_at_the_name_it_lacks",
        a_program_that_does_not_list_read_error_is_one_error_at_the_name_it_lacks,
    ),
];

/// The ways a program lists what it uses of `std.deserialize`: it may list
/// `Problem` or not, and rename either.
const LISTS: &[(&str, &str)] = &[
    ("(ReadError, Problem)", "ReadError"),
    ("(Problem, ReadError)", "ReadError"),
    ("(ReadError)", "ReadError"),
    ("(ReadError as Failed)", "Failed"),
    ("(ReadError as Failed, Problem as Why)", "Failed"),
];

/// What a program prints.
fn prints(source: &str) -> String {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{source}"));
    String::from_utf8(scalar::run(&program).stdout).expect("text")
}

/// The program of the issue, with the way it lists `std.deserialize`.
fn program(list: &str, error: &str) -> String {
    format!(
        "use std.console (console, print)
use std.deserialize {list}
use std.json (from_json, to_json)

struct P {{
  x: Int
}}

fn main() ! console, alloc {{
  r: Result[P, {error}] = from_json(\"\\{{\\\"x\\\":1\\}}\")
  match r {{
    .ok(t) {{ print(to_json(t)) }}
    .err(e) {{ print(\"bad\") }}
  }}
}}
"
    )
}

fn the_program_that_reads_and_writes_a_struct_prints_the_text_it_read() {
    for (list, error) in LISTS {
        let source = program(list, error);
        assert_eq!(prints(&source), "{\"x\":1}\n", "{source}");
    }
}

/// The four settings of `Json`, as the program writes them: a case is written
/// by its name or with a tag, and a name is written as it is or in camel case.
const SETTINGS: [&str; 4] = [
    "Json(cases: .key, names: .written)",
    "Json(cases: .tag(\"type\"), names: .written)",
    "Json(cases: .key, names: .camel)",
    "Json(cases: .tag(\"type\"), names: .camel)",
];

/// A program that writes a value, reads the text, and writes what it read,
/// with `std.deserialize` listed before `std.json`, with the default settings
/// and each of the others.
fn round_trip(g: &mut Gen) -> (String, usize) {
    let mut defs = Defs::default();
    let parts = Parts {
        lists: true,
        own: false,
        barred: false,
    };
    let top = serialize::ty(g, &mut defs, 4, parts);
    let (value, _) = serialize::value(g, &defs, &top);
    let (list, error) = *g.pick(LISTS);
    let name = defs.name(&top);
    let defs_source = defs.source();
    let bits = bits_line(&format!("{defs_source} {name} {value}"));
    let mut source = format!(
        "{bits}use std.console (console, print)
use std.deserialize {list}
use std.json (Json, from_json, from_json_as, to_json, to_json_as)

{}fn main() ! console, alloc {{
  v: {name} = {value}
  t = to_json(v)
  r: Result[{name}, {error}] = from_json(t)
  match r {{
    .ok(w) {{ print(\"{{to_json(w) == t}}\") }}
    .err(e) {{ print(\"read\") }}
  }}
",
        defs_source
    );
    for (k, setting) in SETTINGS.iter().enumerate() {
        source.push_str(&format!(
            "  match to_json_as(v, {setting}) {{
    .ok(t{k}) {{
      r{k}: Result[{name}, {error}] = from_json_as(t{k}, {setting})
      match r{k} {{
        .ok(w) {{
          match to_json_as(w, {setting}) {{
            .ok(u{k}) {{ print(\"{{u{k} == t{k}}}\") }}
            .err(c) {{ print(\"clash\") }}
          }}
        }}
        .err(e) {{ print(\"read\") }}
      }}
    }}
    .err(c) {{ print(\"clash\") }}
  }}
"
        ));
    }
    source.push_str("}\n");
    (source, SETTINGS.len() + 1)
}

fn a_value_written_read_and_written_again_is_the_same_text_each_time_whatever_the_program_lists() {
    generate::check(6101, 6, round_trip, |(source, lines)| {
        let printed = prints(source);
        let printed: Vec<&str> = printed.lines().collect();
        assert_eq!(printed.len(), *lines, "{source}");
        // A setting under which the value cannot be written is not tested.
        for line in printed {
            assert!(line == "true" || line == "clash", "{line}\n{source}");
        }
    });
}

fn a_program_that_does_not_list_read_error_is_one_error_at_the_name_it_lacks() {
    for list in ["(Problem)", ""] {
        let mut source = program(list, "ReadError");
        if list.is_empty() {
            source = source.replace("use std.deserialize \n", "");
        }
        let at = pos_after(&source[..source.find("ReadError]").unwrap()]);
        let errors = check(source.as_bytes()).err().unwrap_or_default();
        assert_eq!(errors.len(), 1, "{errors:?}\n{source}");
        assert_eq!(errors[0].pos, at, "{errors:?}\n{source}");
        assert!(errors[0].message.contains("ReadError"), "{errors:?}");
    }
}

/// The line that imports the sizes the text names, and nothing if it names
/// none.
fn bits_line(text: &str) -> String {
    let mut sizes: Vec<&str> = ["i16", "i32", "i64", "i8", "u16", "u32", "u64", "u8"]
        .into_iter()
        .filter(|n| text.split(|c: char| !(c.is_alphanumeric() || c == '_')).any(|w| w == *n))
        .collect();
    sizes.sort();
    if sizes.is_empty() {
        String::new()
    } else {
        format!("use std.bits ({})\n", sizes.join(", "))
    }
}

/// The source of a module of the library from its start up to the item that
/// starts with `until`, with its `pub` taken out, so that a program defines
/// the same traits, structs, and enums as the library does.
fn own(module: &str, until: &str) -> String {
    let text = library_text(module);
    let text = text[..text.find(until).unwrap()].replace("pub ", "");
    // The program has the `use` lines of its own, at its top.
    let lines: Vec<&str> = text.lines().filter(|l| !l.starts_with("use ")).collect();
    format!("{}\n", lines.join("\n"))
}

fn a_program_that_defines_the_names_of_the_library_beside_the_library_listed_as_other_names_does_what_each_says()
{
    let defined = format!(
        "{}{}{}",
        own("serialize", "pub trait Serialize"),
        own("deserialize", "pub trait Deserialize"),
        serialize::log()
    );
    for (deserialize, serialize) in [
        (
            "use std.deserialize (Problem as LibProblem, ReadError as LibError, Reader as LibReader)\n",
            "use std.serialize (Writer as LibWriter)\n",
        ),
        ("", ""),
    ] {
        let lists = !deserialize.is_empty();
        let error = if lists { "LibError" } else { "ReadError" };
        let source = format!(
            "use std.bits (u64)
use std.console (console, print)
{deserialize}use std.json (from_json, to_json)
{serialize}use std.utf8 (utf8)

struct P {{
  x: Int
}}

{defined}
fn main() ! console, alloc {{
  r: Result[P, {error}] = from_json(\"\\{{\\\"x\\\":1\\}}\")
  match r {{
    .ok(t) {{ print(to_json(t)) }}
    .err(e) {{ print(\"bad\") }}
  }}
  l = Writer.end_structure(Log(lines: \"\"))
  print(l.lines)
  e = ReadError(path: \"p\", problem: Problem.missing(\"x\"), line: 1, column: 2)
  print(\"{{e.path}} {{e.line}} {{e.column}}\")
}}
"
        );
        // Without the library's `ReadError` listed, the result is the
        // program's own, which `from_json` does not give.
        if !lists {
            let errors = check(source.as_bytes()).err().unwrap_or_default();
            assert!(!errors.is_empty(), "{source}");
            continue;
        }
        assert_eq!(
            prints(&source),
            "{\"x\":1}\nend_structure\n\np 1 2\n",
            "{source}"
        );
    }
}
