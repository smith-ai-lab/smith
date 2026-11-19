//! A type is given `Serialize` when its parts have it, and a value writes its
//! shape to any type that has `Writer`, by calling the writer's function for
//! each shape in the order of the value's parts, with nothing built between
//! the value and the writer.
//!
//! The generator builds a type, to a depth of four, from numbers, `bool`,
//! `str`, `()`, `Option`, lists, tuples, structs, and enums, and a value of
//! it. It writes the value as source, and from the value works out the calls
//! the writer must receive. The writer the tests define appends one line of
//! text for each call, so the text it holds is that list of calls.

use smith::diag::Error;
use smith::{check, lower, scalar};

use crate::Test;
use crate::generate::{self, Gen};
use crate::programs::pos_after;

pub const ONCE: &[Test] = &[
    (
        "a_rawjson_written_to_a_writer_of_another_format_is_a_text",
        a_rawjson_written_to_a_writer_of_another_format_is_a_text,
    ),
    (
        "a_struct_writes_its_name_and_each_field_in_order",
        a_struct_writes_its_name_and_each_field_in_order,
    ),
    (
        "every_value_makes_the_calls_of_the_shapes_of_its_parts_in_order",
        every_value_makes_the_calls_of_the_shapes_of_its_parts_in_order,
    ),
    (
        "a_type_taken_from_without_serialize_refuses_every_type_that_holds_it_at_the_call",
        a_type_taken_from_without_serialize_refuses_every_type_that_holds_it_at_the_call,
    ),
    (
        "a_types_own_impl_is_the_one_used_wherever_it_is_a_part",
        a_types_own_impl_is_the_one_used_wherever_it_is_a_part,
    ),
    (
        "a_writer_that_counts_is_called_once_for_each_call_and_asks_for_no_memory",
        a_writer_that_counts_is_called_once_for_each_call_and_asks_for_no_memory,
    ),
    (
        "writing_a_list_asks_for_no_memory_but_the_writers",
        writing_a_list_asks_for_no_memory_but_the_writers,
    ),
    (
        "serialize_and_writer_are_imported_with_use_and_are_not_in_the_prelude",
        serialize_and_writer_are_imported_with_use_and_are_not_in_the_prelude,
    ),
    (
        "a_program_that_never_writes_a_value_is_the_program_it_was",
        a_program_that_never_writes_a_value_is_the_program_it_was,
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

const IMPORT: &str = "use std.serialize (Serialize, Writer, write)\n\n";

/// The functions of `Writer`, which each take the writer and the arguments
/// after it, and say what a writer of each kind does.
const FUNCTIONS: &[(&str, &str)] = &[
    ("boolean", "value: bool"),
    ("signed", "value: i64"),
    ("unsigned", "value: u64"),
    ("text", "value: str"),
    ("nothing", ""),
    ("absent", ""),
    ("present", ""),
    ("sequence", "size: Nat"),
    ("end_sequence", ""),
    ("tuple", "size: Nat"),
    ("end_tuple", ""),
    ("structure", "name: str, fields: Nat"),
    ("field", "name: str"),
    ("end_structure", ""),
    ("case", "of: str, name: str, holds: bool"),
    ("map", "size: Nat"),
    ("end_map", ""),
    ("raw", "of: str, bytes: Slice[u8]"),
];

/// An `impl` of `Writer` for `name`, which gives each function's body, from
/// the function's name and the names of its arguments.
fn writer(name: &str, body: impl Fn(&str, &[&str]) -> String) -> String {
    let mut out = format!("impl Writer[{name}] {{\n");
    for (function, params) in FUNCTIONS {
        let names: Vec<&str> = (params.split(", "))
            .filter(|p| !p.is_empty())
            .map(|p| p.split(':').next().unwrap())
            .collect();
        let params = if params.is_empty() {
            String::new()
        } else {
            format!(", {params}")
        };
        out.push_str(&format!(
            "  fn {function}(w: take {name}{params}) -> {name} ! alloc {{ {} }}\n",
            body(function, &names)
        ));
    }
    out.push_str("}\n\n");
    out
}

/// A writer that appends one line for each call, which names the function
/// and shows its arguments, and the program that prints what it holds.
pub fn log() -> String {
    let body = |function: &str, names: &[&str]| {
        if function == "raw" {
            // The log is of a format that is not the one that `of` names, so
            // the bytes are a text.
            return "match utf8(bytes) {\n      .ok(t) { Log(lines: \"{w.lines}text {t}\\n\") }\n      .err(e) { w }\n    }".to_string();
        }
        let args: String = names.iter().map(|n| format!(" {{{n}}}")).collect();
        format!("Log(lines: \"{{w.lines}}{function}{args}\\n\")")
    };
    format!(
        "struct Log {{\n  lines: str\n}}\n\n{}",
        writer("Log", body)
    )
}

/// A writer that counts the calls, and holds nothing else.
fn count() -> String {
    format!(
        "struct Count {{\n  n: u64\n}}\n\n{}",
        writer("Count", |_, _| "Count(n: wrapping_add(w.n, 1))".to_string())
    )
}

// What the issue says, as it says it.

fn a_struct_writes_its_name_and_each_field_in_order() {
    let source = format!(
        "{IMPORT}{}struct Point {{
  x: i64
  y: i64
}}

fn main() ! console, alloc {{
  l = write(Point(x: 3, y: 4), Log(lines: \"\"))
  print(l.lines)
}}
",
        log()
    );
    let found = errors(&source);
    assert!(found.is_empty(), "{found:?}\n{source}");
    assert_eq!(
        prints(&source),
        "structure Point 2\nfield x\nsigned 3\nfield y\nsigned 4\nend_structure\n\n"
    );
}

/// `raw` names the format that its bytes are in, and a writer of another format
/// writes them as a text, as `text` does.
fn a_rawjson_written_to_a_writer_of_another_format_is_a_text() {
    let source = format!(
        "use std.serialize (Serialize, Writer, write)\nuse std.json (RawJson)\n\n{}struct Call {{\n  args: RawJson\n}}\n\nfn main() ! console, alloc {{\n  l = write(Call(args: RawJson(text: \"[1.50, 1e400]\")), Log(lines: \"\"))\n  print(l.lines)\n}}\n",
        log()
    );
    let found = errors(&source);
    assert!(found.is_empty(), "{found:?}\n{source}");
    assert_eq!(
        prints(&source),
        "structure Call 1\nfield args\ntext [1.50, 1e400]\nend_structure\n\n"
    );
}

// A type of the generator.

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Ty {
    /// A whole number type, by its name.
    Int(&'static str),
    Bool,
    Text,
    Unit,
    Option(Box<Ty>),
    List(Box<Ty>),
    Tuple(Vec<Ty>),
    /// A struct, by its place among the definitions.
    Struct(usize),
    /// An enum, by its place among the definitions.
    Enum(usize),
    /// `Celsius`, which writes itself, or `Session`, which cannot be written.
    Own,
    Barred,
}

/// The whole number types, with each one's least and greatest values and
/// whether it is signed.
pub const INTS: &[(&str, i128, i128)] = &[
    ("i8", -128, 127),
    ("i16", -32768, 32767),
    ("i32", -2147483648, 2147483647),
    ("i64", i64::MIN as i128, i64::MAX as i128),
    ("u8", 0, 255),
    ("u16", 0, 65535),
    ("u32", 0, 4294967295),
    ("u64", 0, u64::MAX as i128),
    ("Nat", 0, u64::MAX as i128),
];

/// The structs and enums a type holds, each by its place, so that a type is
/// written with their names.
#[derive(Default)]
pub struct Defs {
    /// The type of each field of each struct.
    pub structs: Vec<Vec<Ty>>,
    /// The type each case of each enum holds, if it holds one.
    pub enums: Vec<Vec<Option<Ty>>>,
}

impl Defs {
    pub fn name(&self, t: &Ty) -> String {
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
            Ty::Own => "Celsius".to_string(),
            Ty::Barred => "Session".to_string(),
        }
    }

    /// The source that defines each struct and each enum.
    pub fn source(&self) -> String {
        let mut out = String::new();
        for (s, fields) in self.structs.iter().enumerate() {
            out.push_str(&format!("struct S{s} {{\n"));
            for (f, t) in fields.iter().enumerate() {
                out.push_str(&format!("  f{f}: {}\n", self.name(t)));
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

/// What a type is made of: whether lists are allowed, which build their
/// elements in memory, and which of the types that are not numbers may be a
/// part.
#[derive(Clone, Copy)]
pub struct Parts {
    pub lists: bool,
    pub own: bool,
    pub barred: bool,
}

/// A type of at most `depth` levels, which adds the structs and enums it holds
/// to `defs`. The first choices are the simplest.
pub fn ty(g: &mut Gen, defs: &mut Defs, depth: usize, parts: Parts) -> Ty {
    let leaves = 4 + usize::from(parts.own) + usize::from(parts.barred);
    let kinds = if depth == 0 { leaves } else { leaves + 5 };
    let mut at = g.below(kinds);
    if at < 4 {
        return match at {
            0 => Ty::Bool,
            1 => Ty::Int(g.pick(INTS).0),
            2 => Ty::Text,
            _ => Ty::Unit,
        };
    }
    at -= 4;
    if parts.own {
        if at == 0 {
            return Ty::Own;
        }
        at -= 1;
    }
    if parts.barred {
        if at == 0 {
            return Ty::Barred;
        }
        at -= 1;
    }
    match at {
        0 => Ty::Option(Box::new(ty(g, defs, depth - 1, parts))),
        1 if parts.lists => {
            // A list of `()` is not built.
            let element = match ty(g, defs, depth - 1, parts) {
                Ty::Unit => Ty::Bool,
                other => other,
            };
            Ty::List(Box::new(element))
        }
        1 => Ty::Option(Box::new(ty(g, defs, depth - 1, parts))),
        2 => {
            let size = 2 + g.below(2);
            Ty::Tuple((0..size).map(|_| ty(g, defs, depth - 1, parts)).collect())
        }
        3 => {
            let size = 1 + g.below(3);
            let fields: Vec<Ty> = (0..size).map(|_| ty(g, defs, depth - 1, parts)).collect();
            defs.structs.push(fields);
            Ty::Struct(defs.structs.len() - 1)
        }
        _ => {
            let size = 1 + g.below(3);
            let mut cases = Vec::new();
            for _ in 0..size {
                let held = g.one_in(2);
                cases.push(held.then(|| ty(g, defs, depth - 1, parts)));
            }
            defs.enums.push(cases);
            Ty::Enum(defs.enums.len() - 1)
        }
    }
}

/// A value of the type, as source, and the calls the writer must receive for
/// it, one for each line.
fn value(g: &mut Gen, defs: &Defs, t: &Ty) -> (String, Vec<String>) {
    match t {
        Ty::Bool => {
            let b = g.one_in(2);
            (b.to_string(), vec![format!("boolean {b}")])
        }
        Ty::Int(name) => {
            let (_, least, greatest) = *INTS.iter().find(|(n, ..)| n == name).unwrap();
            let n = *g.pick(&[0, 1, least, greatest, 7]);
            let call = if least < 0 { "signed" } else { "unsigned" };
            (n.to_string(), vec![format!("{call} {n}")])
        }
        Ty::Text => {
            let text = *g.pick(&["", "a", "hi there"]);
            (format!("\"{text}\""), vec![format!("text {text}")])
        }
        Ty::Unit => ("()".to_string(), vec!["nothing".to_string()]),
        Ty::Option(of) => {
            if g.one_in(2) {
                ("Option.none".to_string(), vec!["absent".to_string()])
            } else {
                let (source, mut calls) = value(g, defs, of);
                calls.insert(0, "present".to_string());
                (format!("Option.some({source})"), calls)
            }
        }
        Ty::List(of) => {
            let size = g.below(4);
            let mut sources = Vec::new();
            let mut calls = vec![format!("sequence {size}")];
            for _ in 0..size {
                let (source, inner) = value(g, defs, of);
                sources.push(source);
                calls.extend(inner);
            }
            calls.push("end_sequence".to_string());
            (format!("[{}]", sources.join(", ")), calls)
        }
        Ty::Tuple(of) => {
            let mut sources = Vec::new();
            let mut calls = vec![format!("tuple {}", of.len())];
            for t in of {
                let (source, inner) = value(g, defs, t);
                sources.push(source);
                calls.extend(inner);
            }
            calls.push("end_tuple".to_string());
            (format!("({})", sources.join(", ")), calls)
        }
        Ty::Struct(s) => {
            let fields = &defs.structs[*s];
            let mut sources = Vec::new();
            let mut calls = vec![format!("structure S{s} {}", fields.len())];
            for (f, t) in fields.iter().enumerate() {
                let (source, inner) = value(g, defs, t);
                sources.push(format!("f{f}: {source}"));
                calls.push(format!("field f{f}"));
                calls.extend(inner);
            }
            calls.push("end_structure".to_string());
            (format!("S{s}({})", sources.join(", ")), calls)
        }
        Ty::Enum(e) => {
            let cases = &defs.enums[*e];
            let c = g.below(cases.len());
            match &cases[c] {
                Some(t) => {
                    let (source, inner) = value(g, defs, t);
                    let mut calls = vec![format!("case E{e} c{c} true")];
                    calls.extend(inner);
                    (format!("E{e}.c{c}({source})"), calls)
                }
                None => (format!("E{e}.c{c}"), vec![format!("case E{e} c{c} false")]),
            }
        }
        Ty::Own => {
            let n = g.below(40) as i64 - 10;
            (format!("Celsius(degrees: {n})"), vec![format!("signed {n}")])
        }
        Ty::Barred => ("Session(token: \"s\")".to_string(), Vec::new()),
    }
}

/// The types a program that writes a value needs: the definitions, the
/// struct that writes itself, and the one that cannot be written.
fn definitions(defs: &Defs, own_impl: bool) -> String {
    let mut out = format!("{}struct Celsius {{\n  degrees: i64\n}}\n\n", defs.source());
    if own_impl {
        out.push_str(
            "impl Serialize[Celsius] {
  fn write[W: Writer](value: Celsius, to: take W) -> W ! alloc { signed(to, value.degrees) }
}

",
        );
    }
    out.push_str("struct Session {\n  token: str\n}\n\nwithout Serialize for Session\n\n");
    out
}

/// A program that binds a value of the type and writes it to a writer, up to
/// the end of the statement that does, so that what follows is the test's.
#[derive(Debug)]
struct Case {
    source: String,
    /// The place of the call of `write`.
    at: smith::diag::Pos,
    /// The calls the writer receives.
    calls: Vec<String>,
}

fn case(g: &mut Gen, parts: Parts, own_impl: bool, writer: &str, to: &str) -> Case {
    let mut defs = Defs::default();
    let mut top = ty(g, &mut defs, 4, parts);
    // A part that cannot be written, or that writes itself, is somewhere in
    // the type.
    if parts.barred && !holds(&defs, &top, &Ty::Barred) {
        top = Ty::Tuple(vec![top, Ty::Barred]);
    }
    if parts.own && !parts.barred && !holds(&defs, &top, &Ty::Own) {
        top = Ty::Tuple(vec![top, Ty::Own]);
    }
    let (value, calls) = value(g, &defs, &top);
    let head = format!(
        "{IMPORT}{writer}{}fn main() ! console, alloc {{\n  v: {} = {value}\n  l = ",
        definitions(&defs, own_impl),
        defs.name(&top)
    );
    let at = pos_after(&head);
    let source = format!("{head}write(v, {to})\n");
    Case { source, at, calls }
}

/// Whether the type is, or holds, `part`.
pub fn holds(defs: &Defs, t: &Ty, part: &Ty) -> bool {
    t == part
        || match t {
            Ty::Option(of) | Ty::List(of) => holds(defs, of, part),
            Ty::Tuple(of) => of.iter().any(|t| holds(defs, t, part)),
            Ty::Struct(s) => defs.structs[*s].iter().any(|t| holds(defs, t, part)),
            Ty::Enum(e) => defs.enums[*e].iter().flatten().any(|t| holds(defs, t, part)),
            _ => false,
        }
}

fn every_value_makes_the_calls_of_the_shapes_of_its_parts_in_order() {
    let parts = Parts {
        lists: true,
        own: false,
        barred: false,
    };
    let generate = move |g: &mut Gen| {
        case(g, parts, false, &log(), "Log(lines: \"\")")
    };
    generate::check(4481, 25, generate, |c| {
        let source = format!("{}  print(l.lines)\n}}\n", c.source);
        let found = errors(&source);
        assert!(found.is_empty(), "{found:?}\n{source}");
        let expected: String = c.calls.iter().map(|l| format!("{l}\n")).collect();
        assert_eq!(prints(&source), format!("{expected}\n"), "{source}");
    });
}

// A type that cannot be written, and one that writes itself.

fn a_type_taken_from_without_serialize_refuses_every_type_that_holds_it_at_the_call() {
    let parts = Parts {
        lists: true,
        own: false,
        barred: true,
    };
    let generate = move |g: &mut Gen| {
        case(g, parts, false, &log(), "Log(lines: \"\")")
    };
    generate::check(4482, 25, generate, |c| {
        let source = format!("{}  print(l.lines)\n}}\n", c.source);
        let found = errors(&source);
        assert_eq!(found.len(), 1, "{found:?}\n{source}");
        assert_eq!(found[0].pos, c.at, "{source}");
        assert!(found[0].message.contains("`Serialize`"), "{}", found[0]);
        assert!(found[0].message.contains("`Session`"), "{}", found[0]);
    });
}

fn a_types_own_impl_is_the_one_used_wherever_it_is_a_part() {
    let parts = Parts {
        lists: true,
        own: true,
        barred: false,
    };
    let generate = move |g: &mut Gen| {
        case(g, parts, true, &log(), "Log(lines: \"\")")
    };
    generate::check(4483, 25, generate, |c| {
        let source = format!("{}  print(l.lines)\n}}\n", c.source);
        let found = errors(&source);
        assert!(found.is_empty(), "{found:?}\n{source}");
        let expected: String = c.calls.iter().map(|l| format!("{l}\n")).collect();
        assert_eq!(prints(&source), format!("{expected}\n"), "{source}");
    });
}

// What a writer is asked, and what it costs.

/// The program that counts the calls: it prints `ok` when the count is the
/// number of calls the value makes, and `no` otherwise, which builds no text.
fn counted(c: &Case) -> String {
    let n = c.calls.len();
    format!(
        "{}  if l.n == {n} {{ print(\"ok\") }} else {{ print(\"no\") }}\n}}\n",
        c.source
    )
}

fn a_writer_that_counts_is_called_once_for_each_call_and_asks_for_no_memory() {
    // Values with no list, which are built in no memory.
    let parts = Parts {
        lists: false,
        own: false,
        barred: false,
    };
    let generate = move |g: &mut Gen| {
        case(g, parts, false, &count(), "Count(n: 0)")
    };
    generate::check(4484, 25, generate, |c| {
        let source = counted(c);
        let found = errors(&source);
        assert!(found.is_empty(), "{found:?}\n{source}");
        let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{source}"));
        let (outcome, requests) = scalar::run_counting(&program, usize::MAX);
        assert_eq!(outcome.stdout, b"ok\n", "{source}");
        assert!(requests.is_empty(), "{requests:?} for {source}");
    });
}

fn writing_a_list_asks_for_no_memory_but_the_writers() {
    // A list of `n` numbers is built by the program that writes it, and
    // the same list is built by one that does not, so what a program asks
    // for beyond it is what the writing asks for.
    let program = |n: usize, write: bool| {
        let items: Vec<String> = (0..n).map(|i| i.to_string()).collect();
        let tail = if write {
            "  c = write(xs, Count(n: 0))\n  if c.n == ".to_string()
                + &(n + 2).to_string()
                + " { print(\"ok\") } else { print(\"no\") }\n"
        } else {
            format!(
                "  if xs.size == {n} {{ print(\"ok\") }} else {{ print(\"no\") }}\n"
            )
        };
        format!(
            "{IMPORT}{}fn main() ! console, alloc {{\n  xs: List[u32] = [{}]\n{tail}}}\n",
            count(),
            items.join(", ")
        )
    };
    for n in [0, 1, 5, 40] {
        let [with, without] = [true, false].map(|write| {
            let source = program(n, write);
            let found = errors(&source);
            assert!(found.is_empty(), "{found:?}\n{source}");
            let lowered = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{source}"));
            let (outcome, requests) = scalar::run_counting(&lowered, usize::MAX);
            assert_eq!(outcome.stdout, b"ok\n", "{source}");
            requests
        });
        assert_eq!(with, without, "for a list of {n}");
    }
}

// Where `Serialize` and `Writer` are.

fn serialize_and_writer_are_imported_with_use_and_are_not_in_the_prelude() {
    let writes = "struct Point {
  x: i64
}

fn main() ! console, alloc {
  l = write(Point(x: 1), 0)
}
";
    // No name of the module is known to a program that does not import it.
    let found = errors(writes);
    assert!(
        found.iter().any(|e| e.message.contains("`write`")),
        "{found:?}"
    );
    let bound = "struct Count {
  n: u64
}

fn show[W: Writer](w: take W) -> W ! alloc { w }

fn main() ! alloc {
  c = show(Count(n: 0))
}
";
    let found = errors(bound);
    assert!(
        found.iter().any(|e| e.message.contains("`Writer`")),
        "{found:?}"
    );
    // With `use`, the same program asks for a writer, and is given one.
    let imported = format!("{IMPORT}{bound}");
    let found = errors(&imported);
    assert!(
        found.iter().any(|e| e.message.contains("`Writer`") && e.message.contains("`Count`")),
        "{found:?}"
    );
    let written = format!("{IMPORT}{}{}", count(), &bound[bound.find("fn show").unwrap()..]);
    let found = errors(&written);
    assert!(found.is_empty(), "{found:?}\n{written}");
}

fn a_program_that_never_writes_a_value_is_the_program_it_was() {
    let base = "struct Point {
  x: i64
  y: i64
}

fn main() ! console, alloc {
  p = Point(x: 3, y: 4)
  print(\"{p.x} {p.y}\")
}
";
    // The same program, with the module imported and everything that writes
    // a value defined, and a function that writes that nothing calls.
    let extended = format!(
        "{IMPORT}{}fn unused(p: Point) -> Log ! alloc {{ write(p, Log(lines: \"\")) }}\n\n{base}",
        log()
    );
    let (a, b) = (lower(base.as_bytes()).unwrap(), lower(extended.as_bytes()).unwrap());
    assert_eq!(a, b);
}
