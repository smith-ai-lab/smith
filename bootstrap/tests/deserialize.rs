//! A type is given `Deserialize` when its parts have it, and a value is built
//! from any type that has `Reader`, by asking the reader for each shape in the
//! order of the value's parts and building nothing between.
//!
//! The reader the tests define is given a list of shapes and hands them out
//! one for each call, as the writer of `serialize.rs` records them. The
//! generator builds a type, to a depth of four, from numbers, `bool`, `str`,
//! `()`, `Option`, lists, tuples, structs, and enums, and a value of it, and
//! from the value it works out the shapes the reader is given, the calls the
//! reader must be asked, and, for the one that breaks, the path of the error.
//! What was read is checked by writing it back with the writer of
//! `serialize.rs`, which the same value must give the same calls.

use smith::diag::Error;
use smith::{check, lower, scalar};

use crate::Test;
use crate::generate::{self, Gen};
use crate::programs::pos_after;
use crate::serialize::{self, Defs, INTS, Parts, Ty, with_import};

pub const ONCE: &[Test] = &[
    (
        "a_rawjson_is_never_read_from_bytes_a_reader_gives_that_are_not_one_json_value_part_1_of_4",
        || a_rawjson_is_never_read_from_bytes_a_reader_gives_that_are_not_one_json_value(0),
    ),
    (
        "a_rawjson_is_never_read_from_bytes_a_reader_gives_that_are_not_one_json_value_part_2_of_4",
        || a_rawjson_is_never_read_from_bytes_a_reader_gives_that_are_not_one_json_value(1),
    ),
    (
        "a_rawjson_is_never_read_from_bytes_a_reader_gives_that_are_not_one_json_value_part_3_of_4",
        || a_rawjson_is_never_read_from_bytes_a_reader_gives_that_are_not_one_json_value(2),
    ),
    (
        "a_rawjson_is_never_read_from_bytes_a_reader_gives_that_are_not_one_json_value_part_4_of_4",
        || a_rawjson_is_never_read_from_bytes_a_reader_gives_that_are_not_one_json_value(3),
    ),
    (
        "a_rawjson_read_from_a_reader_of_another_format_is_the_bytes_of_its_text",
        a_rawjson_read_from_a_reader_of_another_format_is_the_bytes_of_its_text,
    ),
    (
        "a_struct_is_read_from_its_name_each_field_and_its_end",
        a_struct_is_read_from_its_name_each_field_and_its_end,
    ),
    (
        "the_shapes_the_writer_records_for_every_value_read_back_to_the_same_value",
        the_shapes_the_writer_records_for_every_value_read_back_to_the_same_value,
    ),
    (
        "a_field_left_out_is_none_or_its_default_or_an_error_that_names_it",
        a_field_left_out_is_none_or_its_default_or_an_error_that_names_it,
    ),
    (
        "a_number_outside_its_type_is_range_with_the_types_name",
        a_number_outside_its_type_is_range_with_the_types_name,
    ),
    (
        "a_shape_that_is_not_the_one_asked_for_is_shape",
        a_shape_that_is_not_the_one_asked_for_is_shape,
    ),
    (
        "the_path_of_an_error_is_the_way_to_the_value_that_broke",
        the_path_of_an_error_is_the_way_to_the_value_that_broke,
    ),
    (
        "a_read_that_succeeds_asks_for_no_memory_of_its_own_part_1_of_2",
        || a_read_that_succeeds_asks_for_no_memory_of_its_own(0),
    ),
    (
        "a_read_that_succeeds_asks_for_no_memory_of_its_own_part_2_of_2",
        || a_read_that_succeeds_asks_for_no_memory_of_its_own(1),
    ),
    (
        "a_type_taken_from_without_deserialize_refuses_every_type_that_holds_it_at_the_call",
        a_type_taken_from_without_deserialize_refuses_every_type_that_holds_it_at_the_call,
    ),
    (
        "a_types_own_impl_is_the_one_used_wherever_it_is_a_part",
        a_types_own_impl_is_the_one_used_wherever_it_is_a_part,
    ),
    (
        "deserialize_and_reader_are_imported_with_use_and_are_not_in_the_prelude",
        deserialize_and_reader_are_imported_with_use_and_are_not_in_the_prelude,
    ),
    (
        "a_program_that_never_reads_a_value_is_the_program_it_was",
        a_program_that_never_reads_a_value_is_the_program_it_was,
    ),
];

/// The errors a program gives, which are none when it is accepted.
fn errors(source: &str) -> Vec<Error> {
    match check(source.as_bytes()) {
        Ok(_) => Vec::new(),
        Err(errors) => errors,
    }
}

/// What a program prints.
fn prints(source: &str) -> String {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{source}"));
    String::from_utf8(scalar::run(&program).stdout).expect("text")
}

/// The lines, each ended.
fn lines(of: &[String]) -> String {
    of.iter().map(|l| format!("{l}\n")).collect()
}

/// How a message names a whole number type: the compiler spells `Int` and
/// `Byte` as the sizes they are, `i64` and `u8`, in what it tells of a type.
fn spelled(name: &str) -> &str {
    match name {
        "Int" => "i64",
        "Byte" => "u8",
        other => other,
    }
}

// The reader of the tests.

/// The fields of the reader, which holds what it hands out, how far it is, and
/// what it was asked, and the way to the value it is at.
const FIELDS: &[&str] = &[
    "shapes", "at", "asked", "log", "lenient", "depth", "p1", "p2", "p3", "p4", "k1", "k2", "k3",
    "k4",
];

/// The reader `r` with the fields in `changed` given other values.
fn rd(changed: &[(&str, &str)]) -> String {
    let parts: Vec<String> = FIELDS
        .iter()
        .map(|f| {
            let value = (changed.iter())
                .find(|(n, _)| n == f)
                .map_or(format!("r.{f}"), |(_, v)| v.to_string());
            format!("{f}: {value}")
        })
        .collect();
    format!("Rd({})", parts.join(", "))
}

/// A reader that is at the start of the shapes in the list `xs`, which keeps
/// what it was asked and the path when `log` is true, and passes over a field
/// that the type has none of when `lenient` is true.
fn fresh(xs: &str, log: bool, lenient: bool) -> String {
    format!(
        "Rd(shapes: freeze({xs}), at: 0, asked: \"\", log: {log}, lenient: {lenient}, depth: 0, \
         p1: \"\", p2: \"\", p3: \"\", p4: \"\", k1: 0, k2: 0, k3: 0, k4: 0)"
    )
}

/// The shapes, the reader, and what it does for each call. The path to the
/// value is kept in a slot for each of four levels, which a struct, a list, or a
/// case opens, and is only worked out when `log` says so, so that a reader
/// that does not asks for no memory.
fn reader() -> String {
    let mut out = String::from(
        "enum Shape {
  boolean: bool
  signed: Int
  unsigned: u64
  text: str
  nothing
  absent
  present
  sequence: Nat
  end_sequence
  tuple: Nat
  end_tuple
  structure: str
  field: str
  end_structure
  case: str
  over
}

struct Rd {
  shapes: Slice[Shape]
  at: Nat
  asked: str
  log: bool
  lenient: bool
  depth: Nat
  p1: str
  p2: str
  p3: str
  p4: str
  k1: Int
  k2: Int
  k3: Int
  k4: Int
}

fn peek(r: Rd) -> Shape {
  if r.at < r.shapes.size { r.shapes[r.at] } else { Shape.over }
}

",
    );
    let helper = |out: &mut String, text: &str| out.push_str(&format!("{text}\n\n"));
    helper(
        &mut out,
        &format!(
            "fn step(r: Rd, line: str) -> Rd ! alloc {{
  at = if r.at < r.shapes.size {{ r.at + 1 }} else {{ r.at }}
  if r.log {{ {} }} else {{ {} }}
}}",
            rd(&[("at", "at"), ("asked", "\"{r.asked}{line}\\n\"")]),
            rd(&[("at", "at")])
        ),
    );
    helper(
        &mut out,
        &format!(
            "fn stepa(r: Rd, line: str, arg: str) -> Rd ! alloc {{
  at = if r.at < r.shapes.size {{ r.at + 1 }} else {{ r.at }}
  if r.log {{ {} }} else {{ {} }}
}}",
            rd(&[("at", "at"), ("asked", "\"{r.asked}{line} {arg}\\n\"")]),
            rd(&[("at", "at")])
        ),
    );
    helper(
        &mut out,
        &format!(
            "fn note(r: Rd, line: str) -> Rd ! alloc {{
  if r.log {{ {} }} else {{ r }}
}}",
            rd(&[("asked", "\"{r.asked}{line}\\n\"")])
        ),
    );
    helper(
        &mut out,
        &format!(
            "fn notea(r: Rd, line: str, arg: str) -> Rd ! alloc {{
  if r.log {{ {} }} else {{ r }}
}}",
            rd(&[("asked", "\"{r.asked}{line} {arg}\\n\"")])
        ),
    );
    helper(
        &mut out,
        "fn path_of(r: Rd) -> str {
  match r.depth {
    1 { r.p1 }
    2 { r.p2 }
    3 { r.p3 }
    4 { r.p4 }
    _ { \"\" }
  }
}",
    );
    helper(
        &mut out,
        "fn parent(r: Rd) -> str {
  match r.depth {
    2 { r.p1 }
    3 { r.p2 }
    4 { r.p3 }
    _ { \"\" }
  }
}",
    );
    let slots = |text: &str, with: &dyn Fn(usize) -> String| {
        let arms: String = (1..=4)
            .map(|d| format!("    {d} {{ {} }}\n", with(d)))
            .collect();
        format!("{text}{arms}    _ {{ r }}\n  }}\n}}")
    };
    helper(
        &mut out,
        &slots(
            "fn set(r: Rd, text: str) -> Rd {\n  match r.depth {\n",
            &|d| rd(&[(&format!("p{d}"), "text")]),
        ),
    );
    helper(
        &mut out,
        "fn named(r: Rd, name: str) -> Rd ! alloc {
  if r.log { set(r, \"{parent(r)}.{name}\") } else { r }
}",
    );
    helper(
        &mut out,
        &format!(
            "fn enter(r: Rd) -> Rd {{
  if r.log {{
    match r.depth {{
      0 {{ {} }}
      1 {{ {} }}
      2 {{ {} }}
      3 {{ {} }}
      _ {{ r }}
    }}
  }} else {{ r }}
}}",
            rd(&[("depth", "1"), ("p1", "\"\""), ("k1", "0 - 1")]),
            rd(&[("depth", "2"), ("p2", "r.p1"), ("k2", "0 - 1")]),
            rd(&[("depth", "3"), ("p3", "r.p2"), ("k3", "0 - 1")]),
            rd(&[("depth", "4"), ("p4", "r.p3"), ("k4", "0 - 1")]),
        ),
    );
    helper(
        &mut out,
        &format!(
            "fn leave(r: Rd) -> Rd {{
  if r.log {{
    match r.depth {{
      1 {{ {} }}
      2 {{ {} }}
      3 {{ {} }}
      4 {{ {} }}
      _ {{ r }}
    }}
  }} else {{ r }}
}}",
            rd(&[("depth", "0")]),
            rd(&[("depth", "1")]),
            rd(&[("depth", "2")]),
            rd(&[("depth", "3")]),
        ),
    );
    let arms: String = (1..=4)
        .map(|d| {
            let (k, p) = (format!("k{d}"), format!("p{d}"));
            format!(
                "      {d} {{
        n = wrapping_add(r.{k}, 1)
        {}
      }}\n",
                rd(&[(&k, "n"), (&p, "\"{parent(r)}[{n}]\"")])
            )
        })
        .collect();
    helper(
        &mut out,
        &format!(
            "fn next(r: Rd) -> Rd ! alloc {{
  if r.log {{
    match r.depth {{
{arms}      _ {{ r }}
    }}
  }} else {{ r }}
}}"
        ),
    );
    helper(
        &mut out,
        "fn casing(r: Rd, name: str) -> Rd ! alloc {
  if r.log {
    e = enter(r)
    set(e, \"{parent(e)}/{name}\")
  } else { r }
}",
    );
    helper(
        &mut out,
        "fn wrong(r: Rd, wanted: str) -> ReadError {
  ReadError(path: path_of(r), problem: Problem.shape((wanted, \"\")), line: 0, column: 0)
}",
    );
    helper(
        &mut out,
        "fn show(e: ReadError) -> str ! alloc {
  match e.problem {
    .malformed { \"malformed\" }
    .missing(n) { \"missing {n}\" }
    .shape((wanted, found)) { \"shape {wanted}\" }
    .range(n) { \"range {n}\" }
    .case(n) { \"case {n}\" }
    .unknown(n) { \"unknown {n}\" }
    .twice(n) { \"twice {n}\" }
  }
}",
    );
    // What each function does: the shape it takes, and what it gives.
    let simple = |name: &str, of: &str, shape: &str, value: &str| {
        format!(
            "  fn {name}(r: take Rd) -> (Rd, Result[{of}, ReadError]) ! alloc {{
    match peek(r) {{
      {shape} {{ (step(r, \"{name}\"), Result.ok({value})) }}
      _ {{ (note(r, \"{name}\"), Result.err(wrong(r, \"{name}\"))) }}
    }}
  }}
"
        )
    };
    out.push_str("impl Reader[Rd] {\n");
    out.push_str(&simple("boolean", "bool", ".boolean(v)", "v"));
    out.push_str(&simple("signed", "Int", ".signed(v)", "v"));
    out.push_str(&simple("unsigned", "u64", ".unsigned(v)", "v"));
    out.push_str(&simple("text", "str", ".text(v)", "v"));
    out.push_str(&simple("nothing", "()", ".nothing", "()"));
    out.push_str(
        "  fn raw(r: take Rd, of: str) -> (Rd, Result[Slice[Byte], ReadError]) ! alloc {
    match peek(r) {
      .text(v) { (step(r, \"raw\"), Result.ok(v.bytes)) }
      _ { (note(r, \"raw\"), Result.err(wrong(r, \"raw\"))) }
    }
  }
",
    );
    out.push_str(
        "  fn present(r: take Rd) -> (Rd, Result[bool, ReadError]) ! alloc {
    match peek(r) {
      .present { (step(r, \"present\"), Result.ok(true)) }
      .absent { (step(r, \"present\"), Result.ok(false)) }
      _ { (note(r, \"present\"), Result.err(wrong(r, \"present\"))) }
    }
  }
  fn sequence(r: take Rd) -> (Rd, Result[(), ReadError]) ! alloc {
    match peek(r) {
      .sequence(n) { (enter(step(r, \"sequence\")), Result.ok(())) }
      _ { (note(r, \"sequence\"), Result.err(wrong(r, \"sequence\"))) }
    }
  }
  fn more(r: take Rd) -> (Rd, Result[bool, ReadError]) ! alloc {
    match peek(r) {
      .end_sequence { (leave(step(r, \"more\")), Result.ok(false)) }
      _ { (next(note(r, \"more\")), Result.ok(true)) }
    }
  }
  fn tuple(r: take Rd, size: Nat) -> (Rd, Result[(), ReadError]) ! alloc {
    match peek(r) {
      .tuple(n) {
        if n == size { (step(r, \"tuple\"), Result.ok(())) } else {
          (note(r, \"tuple\"), Result.err(wrong(r, \"tuple\")))
        }
      }
      _ { (note(r, \"tuple\"), Result.err(wrong(r, \"tuple\"))) }
    }
  }
  fn end_tuple(r: take Rd) -> (Rd, Result[(), ReadError]) ! alloc {
    match peek(r) {
      .end_tuple { (step(r, \"end_tuple\"), Result.ok(())) }
      _ { (note(r, \"end_tuple\"), Result.err(wrong(r, \"end_tuple\"))) }
    }
  }
  fn structure(r: take Rd, name: str) -> (Rd, Result[(), ReadError]) ! alloc {
    match peek(r) {
      .structure(n) {
        if n == name { (enter(stepa(r, \"structure\", name)), Result.ok(())) } else {
          (notea(r, \"structure\", name), Result.err(wrong(r, \"structure\")))
        }
      }
      _ { (notea(r, \"structure\", name), Result.err(wrong(r, \"structure\"))) }
    }
  }
  fn field(r: take Rd) -> (Rd, Result[Option[str], ReadError]) ! alloc {
    match peek(r) {
      .field(n) { (named(step(r, \"field\"), n), Result.ok(Option.some(n))) }
      .end_structure { (leave(step(r, \"field\")), Result.ok(Option.none)) }
      _ { (note(r, \"field\"), Result.err(wrong(r, \"field\"))) }
    }
  }
  fn unknown(r: take Rd, name: str) -> (Rd, Result[(), ReadError]) ! alloc {
    if r.lenient { (stepa(r, \"unknown\", name), Result.ok(())) } else {
      error = ReadError(path: path_of(r), problem: Problem.unknown(name), line: 0, column: 0)
      (notea(r, \"unknown\", name), Result.err(error))
    }
  }
  fn case(r: take Rd, of: str) -> (Rd, Result[str, ReadError]) ! alloc {
    match peek(r) {
      .case(n) { (casing(stepa(r, \"case\", of), n), Result.ok(n)) }
      _ { (notea(r, \"case\", of), Result.err(wrong(r, \"case\"))) }
    }
  }
  fn end_case(r: take Rd) -> (Rd, Result[(), ReadError]) ! alloc {
    (leave(note(r, \"end_case\")), Result.ok(()))
  }
  fn fail(r: take Rd, problem: Problem) -> (Rd, ReadError) ! alloc {
    (note(r, \"fail\"), ReadError(path: path_of(r), problem: problem, line: 0, column: 0))
  }
}

",
    );
    out
}

// What the issue says, as it says it.

fn a_struct_is_read_from_its_name_each_field_and_its_end() {
    let source = with_import(&[], &format!(
        "{}struct Point {{
  x: Int
  y: Int
}}

fn main() ! console, alloc {{
  xs: List[Shape] = [Shape.structure(\"Point\"), Shape.field(\"x\"), Shape.signed(3), Shape.field(\"y\"), Shape.signed(4), Shape.end_structure]
  (r, p): (Rd, Result[Point, ReadError]) = read({})
  print(r.asked)
  match p {{
    .ok(v) {{ print(\"{{v.x}} {{v.y}}\") }}
    .err(e) {{ print(show(e)) }}
  }}
}}
",
        reader(),
        fresh("xs", true, false)
    ));
    let found = errors(&source);
    assert!(found.is_empty(), "{found:?}\n{source}");
    assert_eq!(
        prints(&source),
        "structure Point\nfield\nsigned\nfield\nsigned\nfield\n\n3 4\n"
    );
}

/// `raw` names the format that its bytes are in, and a reader of another format
/// reads a text and gives its bytes.
fn a_rawjson_read_from_a_reader_of_another_format_is_the_bytes_of_its_text() {
    let source = with_import(
        &[("std.json", &["RawJson", "raw_text"])],
        &format!(
            "{}fn main() ! console, alloc {{\n  xs: List[Shape] = [Shape.text(\"[1.50, 1e400]\")]\n  (r, v): (Rd, Result[RawJson, ReadError]) = read({})\n  match v {{\n    .ok(x) {{ print(raw_text(x)) }}\n    .err(e) {{ print(show(e)) }}\n  }}\n}}\n",
            reader(),
            fresh("xs", false, false)
        ),
    );
    let found = errors(&source);
    assert!(found.is_empty(), "{found:?}\n{source}");
    assert_eq!(prints(&source), "[1.50, 1e400]\n");
}

/// A reader of a program gives the bytes of its text as the bytes of the next
/// value, whatever the format it is asked for, and what is not one whole JSON
/// value is an error and no `RawJson`. What is one is held with the space
/// round it left out.
fn a_rawjson_is_never_read_from_bytes_a_reader_gives_that_are_not_one_json_value(part: u64) {
    const NOT: &[&str] = &["] not json", "[", "1 2", "", "tru", "[1,]"];
    const WHOLE: &[&str] = &["[1.50, 1e400]", " [true] ", "7", " null"];
    generate::check_part(3, 12, part, 4,
        |g: &mut Gen| (g.one_in(2), g.below(NOT.len().max(WHOLE.len()))),
        |&(whole, at)| {
            let list = if whole { WHOLE } else { NOT };
            let text = list[at % list.len()];
            let source = with_import(
                &[("std.json", &["RawJson", "raw_text"])],
                &format!(
                    "{}fn main() ! console, alloc {{\n  xs: List[Shape] = [Shape.text(\"{text}\")]\n  (r, v): (Rd, Result[RawJson, ReadError]) = read({})\n  match v {{\n    .ok(x) {{ print(\"ok {{raw_text(x)}}\") }}\n    .err(e) {{ print(\"err {{show(e)}}\") }}\n  }}\n}}\n",
                    reader(),
                    fresh("xs", false, false)
                ),
            );
            let found = errors(&source);
            assert!(found.is_empty(), "{found:?}\n{source}");
            let said = prints(&source);
            if whole {
                assert_eq!(said, format!("ok {}\n", text.trim()), "{source}");
            } else {
                assert!(said.starts_with("err "), "{said} for\n{source}");
            }
        },
    );
}

// What the generator makes of a value: the shapes the reader is given, the
// calls it must be asked, and what the writer of `serialize.rs` records.

/// A leaf of the value, which is a number, a `bool`, a text, or `()`, and can
/// be broken: its place among the shapes and among the calls, the way to it,
/// and the function of the reader that asks for it.
#[derive(Debug)]
struct Leaf {
    shape: usize,
    asked: usize,
    path: String,
    wanted: &'static str,
}

#[derive(Debug, Default)]
struct Made {
    shapes: Vec<String>,
    asked: Vec<String>,
    written: Vec<String>,
    leaves: Vec<Leaf>,
}

impl Made {
    fn shape(&mut self, shape: String, asked: &str, written: String) {
        self.shapes.push(shape);
        self.asked.push(asked.to_string());
        self.written.push(written);
    }

    fn leaf(&mut self, shape: String, wanted: &'static str, written: String, path: &str) {
        self.leaves.push(Leaf {
            shape: self.shapes.len(),
            asked: self.asked.len(),
            path: path.to_string(),
            wanted,
        });
        self.shape(shape, wanted, written);
    }
}

/// A value of the type, as source, with what it makes in `made`. `path` is
/// the way to the value that a reader works out from where it is.
fn value(g: &mut Gen, defs: &Defs, t: &Ty, path: &str, made: &mut Made) -> String {
    match t {
        Ty::Bool => {
            let b = g.one_in(2);
            made.leaf(
                format!("Shape.boolean({b})"),
                "boolean",
                format!("boolean {b}"),
                path,
            );
            b.to_string()
        }
        Ty::Int(name) => {
            let (_, least, greatest) = *INTS.iter().find(|(n, ..)| n == name).unwrap();
            let n = *g.pick(&[0, 1, least, greatest, 7]);
            let signed = least < 0;
            let (call, shape) = if signed {
                ("signed", "Shape.signed")
            } else {
                ("unsigned", "Shape.unsigned")
            };
            made.leaf(format!("{shape}({n})"), call, format!("{call} {n}"), path);
            n.to_string()
        }
        Ty::Text => {
            let text = *g.pick(&["", "a", "hi there"]);
            made.leaf(
                format!("Shape.text(\"{text}\")"),
                "text",
                format!("text {text}"),
                path,
            );
            format!("\"{text}\"")
        }
        Ty::Unit => {
            made.leaf(
                "Shape.nothing".to_string(),
                "nothing",
                "nothing".to_string(),
                path,
            );
            "()".to_string()
        }
        Ty::Option(of) => {
            if g.one_in(2) {
                made.shape("Shape.absent".to_string(), "present", "absent".to_string());
                "Option.none".to_string()
            } else {
                made.shape(
                    "Shape.present".to_string(),
                    "present",
                    "present".to_string(),
                );
                format!("Option.some({})", value(g, defs, of, path, made))
            }
        }
        Ty::List(of) => {
            let size = g.below(4);
            made.shape(
                "Shape.sequence(".to_string() + &size.to_string() + ")",
                "sequence",
                format!("sequence {size}"),
            );
            let mut sources = Vec::new();
            for k in 0..size {
                made.asked.push("more".to_string());
                sources.push(value(g, defs, of, &format!("{path}[{k}]"), made));
            }
            made.shape(
                "Shape.end_sequence".to_string(),
                "more",
                "end_sequence".to_string(),
            );
            format!("[{}]", sources.join(", "))
        }
        Ty::Tuple(of) => {
            made.shape(
                format!("Shape.tuple({})", of.len()),
                "tuple",
                format!("tuple {}", of.len()),
            );
            let sources: Vec<String> = of
                .iter()
                .map(|t| value(g, defs, t, path, made))
                .collect();
            made.shape(
                "Shape.end_tuple".to_string(),
                "end_tuple",
                "end_tuple".to_string(),
            );
            format!("({})", sources.join(", "))
        }
        Ty::Struct(s) => {
            let fields = &defs.structs[*s];
            made.shape(
                format!("Shape.structure(\"S{s}\")"),
                &format!("structure S{s}"),
                format!("structure S{s} {}", fields.len()),
            );
            let mut sources = Vec::new();
            for (f, t) in fields.iter().enumerate() {
                made.shape(
                    format!("Shape.field(\"f{f}\")"),
                    "field",
                    format!("field f{f}"),
                );
                let source = value(g, defs, t, &format!("{path}.f{f}"), made);
                sources.push(format!("f{f}: {source}"));
            }
            made.shape(
                "Shape.end_structure".to_string(),
                "field",
                "end_structure".to_string(),
            );
            format!("S{s}({})", sources.join(", "))
        }
        Ty::Enum(e) => {
            let cases = &defs.enums[*e];
            let c = g.below(cases.len());
            let holds = cases[c].is_some();
            made.shape(
                format!("Shape.case(\"c{c}\")"),
                &format!("case E{e}"),
                format!("case E{e} c{c} {holds}"),
            );
            let source = match &cases[c] {
                Some(t) => {
                    let inner = value(g, defs, t, &format!("{path}/c{c}"), made);
                    format!("E{e}.c{c}({inner})")
                }
                None => format!("E{e}.c{c}"),
            };
            made.asked.push("end_case".to_string());
            source
        }
        Ty::Own => {
            let n = g.below(40) as i64 - 10;
            made.leaf(
                format!("Shape.signed({n})"),
                "signed",
                format!("signed {n}"),
                path,
            );
            format!("Celsius(degrees: {n})")
        }
        Ty::Barred => "Session(token: \"s\")".to_string(),
    }
}

/// The definitions a program that reads a value needs: those of the type, the
/// struct that reads and writes itself, and the one that cannot be read.
fn definitions(defs: &Defs, own_impl: bool) -> String {
    let mut out = format!("{}struct Celsius {{\n  degrees: Int\n}}\n\n", defs.source());
    if own_impl {
        out.push_str(
            "impl Deserialize[Celsius] {
  fn read[R: Reader](from: take R) -> (R, Result[Celsius, ReadError]) ! alloc {
    (r, n) = Reader.signed(from)
    match n {
      .ok(d) { (r, Result.ok(Celsius(degrees: d))) }
      .err(e) { (r, Result.err(e)) }
    }
  }
}

impl Serialize[Celsius] {
  fn write[W: Writer](value: Celsius, to: take W) -> W ! alloc { Writer.signed(to, value.degrees) }
}

",
        );
    }
    out.push_str(
        "struct Session {\n  token: str\n}\n\nwithout Deserialize for Session\n\n",
    );
    out
}

/// A program that builds a value of a type, reads it from its shapes, and
/// stops where what follows is the test's.
#[derive(Debug)]
struct Case {
    source: String,
    /// The place of the call of `read`.
    at: smith::diag::Pos,
    made: Made,
}

/// `writes` is whether what follows writes the value that was read.
fn case(g: &mut Gen, parts: Parts, own_impl: bool, log: bool, writes: bool) -> Case {
    let mut defs = Defs::default();
    let mut top = serialize::ty(g, &mut defs, 4, parts);
    if parts.barred && !serialize::holds(&defs, &top, &Ty::Barred) {
        top = Ty::Tuple(vec![top, Ty::Barred]);
    }
    if parts.own && !parts.barred && !serialize::holds(&defs, &top, &Ty::Own) {
        top = Ty::Tuple(vec![top, Ty::Own]);
    }
    let mut made = Made::default();
    value(g, &defs, &top, "", &mut made);
    let write: &[&str] = if writes { &["write"] } else { &[] };
    let head = with_import(
        &[
            ("std.deserialize", &["read"]),
            ("std.list", &["freeze"]),
            ("std.serialize", write),
        ],
        &format!(
            "{}{}{}fn main() ! console, alloc {{\n  xs: List[Shape] = [{}]\n  (r, v): (Rd, Result[{}, ReadError]) = ",
            reader(),
            serialize::log(),
            definitions(&defs, own_impl),
            made.shapes.join(", "),
            defs.name(&top)
        ),
    );
    let at = pos_after(&head);
    let source = format!("{head}read({})\n", fresh("xs", log, false));
    Case { source, at, made }
}

/// What a program that reads a value and writes what it read prints, when
/// the read succeeds.
const WRITES: &str = "  print(r.asked)
  match v {
    .ok(x) {
      l = write(x, Log(lines: \"\"))
      print(l.lines)
    }
    .err(e) { print(\"{e.path}|{show(e)}\") }
  }
}
";

fn the_shapes_the_writer_records_for_every_value_read_back_to_the_same_value() {
    let parts = Parts {
        lists: true,
        own: false,
        barred: false,
    };
    let generate = move |g: &mut Gen| case(g, parts, false, true, true);
    generate::check(4581, 8, generate, |c| {
        let source = format!("{}{WRITES}", c.source);
        let found = errors(&source);
        assert!(found.is_empty(), "{found:?}\n{source}");
        let expected = format!(
            "{}\n{}\n",
            lines(&c.made.asked),
            lines(&c.made.written)
        );
        assert_eq!(prints(&source), expected, "{source}");
    });
}

// A struct with fields that may be left out.

/// The kinds of field a struct has.
#[derive(Clone, Copy, Debug, PartialEq)]
enum Kind {
    Plain,
    Option,
    Default,
}

/// The types of field: the name, the default as source, the call the writer
/// records for the default, the shape the input gives, and its call.
const BASES: &[(&str, &str, &str, &str, &str)] = &[
    ("Int", "7", "signed 7", "Shape.signed(5)", "signed 5"),
    ("Byte", "9", "unsigned 9", "Shape.unsigned(3)", "unsigned 3"),
    ("bool", "true", "boolean true", "Shape.boolean(false)", "boolean false"),
    ("str", "\"d\"", "text d", "Shape.text(\"x\")", "text x"),
];

/// What a field of the input is: its place among the struct's fields, and
/// whether it is one the struct has none of.
#[derive(Clone, Copy, Debug)]
enum Given {
    Field(usize),
    Unknown,
}

fn a_field_left_out_is_none_or_its_default_or_an_error_that_names_it() {
    let generate = |g: &mut Gen| {
        let size = 1 + g.below(3);
        let fields: Vec<(Kind, usize)> = (0..size)
            .map(|_| {
                let kind = *g.pick(&[Kind::Plain, Kind::Option, Kind::Default]);
                (kind, g.below(BASES.len()))
            })
            .collect();
        // For each subset of the fields, the input: the order they come in,
        // a field given twice, and a field the struct has none of.
        let inputs: Vec<Vec<Given>> = (0..1usize << size)
            .map(|mask| {
                let mut given: Vec<Given> = (0..size)
                    .filter(|f| mask >> f & 1 == 1)
                    .map(Given::Field)
                    .collect();
                // The order of the input is any.
                for i in (1..given.len()).rev() {
                    given.swap(i, g.below(i + 1));
                }
                if !given.is_empty() && g.one_in(4) {
                    let again = given[g.below(given.len())];
                    let at = g.below(given.len() + 1);
                    given.insert(at, again);
                }
                if g.one_in(4) {
                    let at = g.below(given.len() + 1);
                    given.insert(at, Given::Unknown);
                }
                given
            })
            .collect();
        (fields, inputs, g.one_in(2))
    };
    generate::check(4583, 6, generate, |(fields, inputs, lenient)| {
        let mut source = format!("{}{}", reader(), serialize::log());
        source.push_str("struct Tool {\n");
        for (f, (kind, base)) in fields.iter().enumerate() {
            let (name, default, ..) = BASES[*base];
            source.push_str(&match kind {
                Kind::Plain => format!("  f{f}: {name}\n"),
                Kind::Option => format!("  f{f}: Option[{name}]\n"),
                Kind::Default => format!("  f{f}: {name} = {default}\n"),
            });
        }
        source.push_str("}\n\nstruct Wrap {\n  inner: Tool\n}\n\n");
        source.push_str(
            "fn report(v: Result[Wrap, ReadError]) ! console, alloc {
  match v {
    .ok(x) {
      l = write(x, Log(lines: \"\"))
      print(l.lines)
    }
    .err(e) { print(\"{e.path}|{show(e)}\") }
  }
}

fn main() ! console, alloc {
",
        );
        let mut expected = String::new();
        for (m, input) in inputs.iter().enumerate() {
            let mut shapes = vec![
                "Shape.structure(\"Wrap\")".to_string(),
                "Shape.field(\"inner\")".to_string(),
                "Shape.structure(\"Tool\")".to_string(),
            ];
            for given in input {
                match given {
                    Given::Field(f) => {
                        let (kind, base) = fields[*f];
                        shapes.push(format!("Shape.field(\"f{f}\")"));
                        if kind == Kind::Option {
                            shapes.push("Shape.present".to_string());
                        }
                        shapes.push(BASES[base].3.to_string());
                    }
                    Given::Unknown => {
                        shapes.push("Shape.field(\"zz\")".to_string());
                        shapes.push("Shape.signed(1)".to_string());
                    }
                }
            }
            shapes.push("Shape.end_structure".to_string());
            shapes.push("Shape.end_structure".to_string());
            source.push_str(&format!(
                "  xs{m}: List[Shape] = [{}]\n  (r{m}, v{m}): (Rd, Result[Wrap, ReadError]) = read({})\n  report(v{m})\n",
                shapes.join(", "),
                fresh(&format!("xs{m}"), true, *lenient)
            ));
            // What the first thing wrong is, which is the first of what
            // the input holds that is wrong, and else the first plain
            // field that is left out.
            let mut seen = vec![false; fields.len()];
            let mut wrong = None;
            for given in input {
                match given {
                    Given::Field(f) if seen[*f] => {
                        wrong = Some(format!(".inner.f{f}|twice f{f}"));
                        break;
                    }
                    Given::Field(f) => seen[*f] = true,
                    Given::Unknown if !*lenient => {
                        wrong = Some(".inner.zz|unknown zz".to_string());
                        break;
                    }
                    Given::Unknown => {}
                }
            }
            if wrong.is_none() {
                wrong = (fields.iter().enumerate())
                    .find(|(f, (kind, _))| *kind == Kind::Plain && !seen[*f])
                    .map(|(f, _)| format!(".inner|missing f{f}"));
            }
            match wrong {
                Some(wrong) => expected.push_str(&format!("{wrong}\n")),
                None => {
                    let mut written = vec![
                        "structure Wrap 1".to_string(),
                        "field inner".to_string(),
                        format!("structure Tool {}", fields.len()),
                    ];
                    for (f, (kind, base)) in fields.iter().enumerate() {
                        written.push(format!("field f{f}"));
                        let (_, _, default, _, given) = BASES[*base];
                        match (seen[f], kind) {
                            (true, Kind::Option) => {
                                written.push("present".to_string());
                                written.push(given.to_string());
                            }
                            (true, _) => written.push(given.to_string()),
                            (false, Kind::Option) => written.push("absent".to_string()),
                            (false, _) => written.push(default.to_string()),
                        }
                    }
                    written.push("end_structure".to_string());
                    written.push("end_structure".to_string());
                    expected.push_str(&format!("{}\n", lines(&written)));
                }
            }
        }
        source.push_str("}\n");
        let source = with_import(&[], &source);
        let found = errors(&source);
        assert!(found.is_empty(), "{found:?}\n{source}");
        assert_eq!(prints(&source), expected, "{source}");
    });
}

// A number that does not fit, and a shape that is not the one asked for.

fn a_number_outside_its_type_is_range_with_the_types_name() {
    let mut source = format!("{}fn main() ! console, alloc {{\n", reader());
    let mut expected = String::new();
    let mut at = 0;
    for (name, least, greatest) in INTS {
        let signed = *least < 0;
        let shape = if signed { "Shape.signed" } else { "Shape.unsigned" };
        let narrow = !matches!(*name, "Int" | "u64" | "Nat");
        // The least and the greatest, and one past each when the type is
        // narrower than the numbers a reader gives.
        let mut numbers = vec![(*least, true), (*greatest, true)];
        if narrow {
            numbers.push((greatest + 1, false));
            if signed {
                numbers.push((least - 1, false));
            }
        }
        for (n, fits) in numbers {
            source.push_str(&format!(
                "  xs{at}: List[Shape] = [{shape}({n})]
  (r{at}, v{at}): (Rd, Result[{name}, ReadError]) = read({})
  match v{at} {{
    .ok(x) {{ print(\"{{x}}\") }}
    .err(e) {{ print(show(e)) }}
  }}
",
                fresh(&format!("xs{at}"), true, false)
            ));
            expected.push_str(&if fits {
                format!("{n}\n")
            } else {
                format!("range {}\n", spelled(name))
            });
            at += 1;
        }
    }
    source.push_str("}\n");
    let source = with_import(&[], &source);
    let found = errors(&source);
    assert!(found.is_empty(), "{found:?}\n{source}");
    assert_eq!(prints(&source), expected, "{source}");
}

fn a_shape_that_is_not_the_one_asked_for_is_shape() {
    let parts = Parts {
        lists: true,
        own: false,
        barred: false,
    };
    // The first shape is not the one the type asks for, whatever the type.
    let generate = move |g: &mut Gen| case(g, parts, false, true, true);
    generate::check(4584, 8, generate, |c| {
        let mut made = c.source.clone();
        let first = &c.made.shapes[0];
        made = made.replacen(first, "Shape.over", 1);
        let source = format!("{made}{WRITES}");
        let found = errors(&source);
        assert!(found.is_empty(), "{found:?}\n{source}");
        // A call is named for the function that makes it.
        let asked = &c.made.asked[0];
        let wanted = asked.split(' ').next().unwrap();
        let expected = format!("{}\n|shape {wanted}\n", lines(&c.made.asked[..1]));
        assert_eq!(prints(&source), expected, "{source}");
    });
}

fn the_path_of_an_error_is_the_way_to_the_value_that_broke() {
    let parts = Parts {
        lists: true,
        own: false,
        barred: false,
    };
    let generate = move |g: &mut Gen| {
        let c = case(g, parts, false, true, true);
        let at = g.below(c.made.leaves.len().max(1));
        (c, at)
    };
    generate::check(4585, 8, generate, |(c, at)| {
        let Some(leaf) = c.made.leaves.get(*at) else {
            return;
        };
        // The leaf is given a shape that is not the one it is asked for.
        let mut shapes = c.made.shapes.clone();
        shapes[leaf.shape] = "Shape.over".to_string();
        let old = format!("[{}]", c.made.shapes.join(", "));
        let new = format!("[{}]", shapes.join(", "));
        let source = format!("{}{WRITES}", c.source.replacen(&old, &new, 1));
        let found = errors(&source);
        assert!(found.is_empty(), "{found:?}\n{source}");
        let expected = format!(
            "{}\n{}|shape {}\n",
            lines(&c.made.asked[..=leaf.asked]),
            leaf.path,
            leaf.wanted
        );
        assert_eq!(prints(&source), expected, "{source}");
    });
}

// What reading costs.

fn a_read_that_succeeds_asks_for_no_memory_of_its_own(part: u64) {
    // Values with no list, read by a reader that keeps nothing it was
    // asked, and so asks for no memory itself. The shapes it is given are
    // built by the program that reads them and by one that does not, so
    // what a program asks for beyond that is what the reading asks for.
    let parts = Parts {
        lists: false,
        own: false,
        barred: false,
    };
    let generate = move |g: &mut Gen| case(g, parts, false, false, false);
    generate::check_part(4586, 8, part, 2, generate, |c| {
        let reads = format!(
            "{}  match v {{\n    .ok(x) {{ print(\"ok\") }}\n    .err(e) {{ print(\"no\") }}\n  }}\n}}\n",
            c.source
        );
        let head = &c.source[..c.source.rfind("  (r, v)").unwrap()];
        let skips = format!("{head}  print(\"ok\")\n}}\n");
        let [with, without] = [&reads, &skips].map(|source| {
            let found = errors(source);
            assert!(found.is_empty(), "{found:?}\n{source}");
            let program =
                lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{source}"));
            let (outcome, requests) = scalar::run_counting(&program, usize::MAX);
            assert_eq!(outcome.stdout, b"ok\n", "{source}");
            requests
        });
        assert_eq!(with, without, "{}", c.source);
    });
}

// A type that cannot be read, and one that reads itself.

fn a_type_taken_from_without_deserialize_refuses_every_type_that_holds_it_at_the_call() {
    let parts = Parts {
        lists: true,
        own: false,
        barred: true,
    };
    let generate = move |g: &mut Gen| case(g, parts, false, true, false);
    generate::check(4582, 8, generate, |c| {
        let source = format!("{}  print(r.asked)\n}}\n", c.source);
        let found = errors(&source);
        assert_eq!(found.len(), 1, "{found:?}\n{source}");
        assert_eq!(found[0].pos, c.at, "{source}");
        assert!(found[0].message.contains("`Deserialize`"), "{}", found[0]);
        assert!(found[0].message.contains("`Session`"), "{}", found[0]);
    });
}

fn a_types_own_impl_is_the_one_used_wherever_it_is_a_part() {
    let parts = Parts {
        lists: true,
        own: true,
        barred: false,
    };
    let generate = move |g: &mut Gen| case(g, parts, true, true, true);
    generate::check(4587, 8, generate, |c| {
        let source = format!("{}{WRITES}", c.source);
        let found = errors(&source);
        assert!(found.is_empty(), "{found:?}\n{source}");
        let expected = format!(
            "{}\n{}\n",
            lines(&c.made.asked),
            lines(&c.made.written)
        );
        assert_eq!(prints(&source), expected, "{source}");
    });
}

// Where `Deserialize` and `Reader` are.

fn deserialize_and_reader_are_imported_with_use_and_are_not_in_the_prelude() {
    let reads = "struct Point {\n  x: Int\n}\n\nfn main() {\n  v = read(0)\n}\n";
    // No name of the module is known to a program that does not import it.
    let found = errors(reads);
    assert!(
        found.iter().any(|e| e.message.contains("`read`")),
        "{found:?}"
    );
    let body = "struct Count {\n  n: u64\n}\n\nfn pass[R: Reader](r: take R) -> R ! alloc { r }\n\nfn main() ! alloc {\n  c = pass(Count(n: 0))\n}\n";
    let bound = format!("use std.bits (u64)\n\n{body}");
    let bound = bound.as_str();
    let found = errors(bound);
    assert!(
        found.iter().any(|e| e.message.contains("`Reader`")),
        "{found:?}"
    );
    // With `use`, the same program asks for a reader, and is given one.
    let imported = format!("use std.bits (u64)\nuse std.deserialize (Reader)\n\n{body}");
    let found = errors(&imported);
    assert!(
        found.iter().any(|e| e.message.contains("`Reader`") && e.message.contains("`Count`")),
        "{found:?}"
    );
    let read = with_import(
        &[],
        &format!(
            "{}{}",
            reader(),
            "fn pass[R: Reader](r: take R) -> R ! alloc { r }\n\nfn main() ! alloc {\n  xs: List[Shape] = [Shape.over]\n  c = pass(RD)\n}\n"
                .replace("RD", &fresh("xs", true, false))
        ),
    );
    let found = errors(&read);
    assert!(found.is_empty(), "{found:?}\n{read}");
}

fn a_program_that_never_reads_a_value_is_the_program_it_was() {
    let items = "struct Point {\n  x: Int\n  y: Int\n}\n\nfn main() ! console, alloc {\n  p = Point(x: 3, y: 4)\n  print(\"{p.x} {p.y}\")\n}\n";
    let base = format!("use std.console (console, print)\n\n{items}");
    // The same program, with the module imported and everything that reads a
    // value defined, and a function that reads that nothing calls.
    let extended = with_import(
        &[],
        &format!(
            "{}fn unused(r: Rd) -> (Rd, Result[Point, ReadError]) ! alloc {{ read(r) }}\n\n{items}",
            reader()
        ),
    );
    let (a, b) = (lower(base.as_bytes()).unwrap(), lower(extended.as_bytes()).unwrap());
    assert_eq!(a, b);
}
