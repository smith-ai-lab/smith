//! A type is given `Describe` when its parts have it, and describes its shape
//! to any type that has `Describer`, by calling the describer's function for
//! each shape in the order of the type's parts, each struct, field, enum, and
//! case with its name and its comment, with no value in hand.
//!
//! The generator builds a type, to a depth of three, from numbers, `bool`,
//! `str`, `()`, `Option`, lists, tuples, structs, and enums, writes some of
//! the comments on them, and from the type works out the calls the describer
//! must receive. The describer the tests define appends one line of text for
//! each call, so the text it holds is that list of calls.

use smith::diag::Error;
use smith::{check, lower, scalar};

use crate::Test;
use crate::generate::{self, Gen};
use crate::targets;
use crate::serialize::{Defs, INTS, Parts, Ty, ty, with_import};

/// The executables of the programs are checked against the evaluator.
pub const MACHINE: &[Test] = &[(
    "every_executable_of_a_description_prints_what_the_evaluator_says",
    every_executable_of_a_description_prints_what_the_evaluator_says,
)];

pub const ONCE: &[Test] = &[
    (
        "a_type_describes_the_calls_of_the_shapes_of_its_parts_in_order",
        a_type_describes_the_calls_of_the_shapes_of_its_parts_in_order,
    ),
    (
        "a_range_describes_its_least_and_its_greatest_value",
        a_range_describes_its_least_and_its_greatest_value,
    ),
    (
        "a_type_taken_from_without_describe_refuses_every_type_that_holds_it",
        a_type_taken_from_without_describe_refuses_every_type_that_holds_it,
    ),
    (
        "a_types_own_impl_is_the_description_used_wherever_it_is_a_part",
        a_types_own_impl_is_the_description_used_wherever_it_is_a_part,
    ),
    (
        "the_arguments_of_a_function_are_described_with_the_comments_on_it_and_on_each_parameter",
        the_arguments_of_a_function_are_described_with_the_comments_on_it_and_on_each_parameter,
    ),
    (
        "a_program_that_describes_nothing_holds_no_comment",
        a_program_that_describes_nothing_holds_no_comment,
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

/// The functions of `Describer`, each with the arguments after the describer.
const FUNCTIONS: &[(&str, &str)] = &[
    ("boolean", ""),
    ("signed", "least: Int, greatest: Int"),
    ("unsigned", "least: u64, greatest: u64"),
    ("text", ""),
    ("nothing", ""),
    ("optional", ""),
    ("sequence", ""),
    ("tuple", "size: Nat"),
    ("structure", "name: str, about: str, fields: Nat"),
    ("field", "name: str, about: str, optional: bool"),
    ("end_structure", ""),
    ("enumeration", "name: str, about: str, cases: Nat"),
    ("case", "name: str, about: str, holds: bool"),
    ("end_enumeration", ""),
    ("map", ""),
];

/// A describer that appends one line for each call, which names the function
/// and shows its arguments, and no more of the program than that.
fn log() -> String {
    let mut out = "struct Log {\n  lines: str\n}\n\nimpl Describer[Log] {\n".to_string();
    for (function, params) in FUNCTIONS {
        let names: Vec<&str> = (params.split(", "))
            .filter(|p| !p.is_empty())
            .map(|p| p.split(':').next().unwrap())
            .collect();
        let args: String = names.iter().map(|n| format!(" {{{n}}}")).collect();
        let params = if params.is_empty() {
            String::new()
        } else {
            format!(", {params}")
        };
        out.push_str(&format!(
            "  fn {function}(d: take Log{params}) -> Log ! alloc {{ Log(lines: \"{{d.lines}}{function}{args}\\n\") }}\n"
        ));
    }
    out.push_str("}\n\n");
    out
}

/// A generated type with the program that describes it, and the calls the
/// describer must receive, one for each line.
#[derive(Debug)]
struct Case {
    source: String,
    calls: Vec<String>,
}

/// The comment of each struct, field, enum, and case that has one, by the name
/// the generator gives it, and what each field has as a default.
/// A field's comment and whether it has a default.
type FieldNote = (Option<String>, bool);

struct Notes {
    structs: Vec<(Option<String>, Vec<FieldNote>)>,
    enums: Vec<(Option<String>, Vec<Option<String>>)>,
}

fn about(g: &mut Gen, name: &str) -> Option<String> {
    g.one_in(2).then(|| format!("About {name}."))
}

/// The notes of the structs and enums the generator made.
fn notes(g: &mut Gen, defs: &Defs) -> Notes {
    let structs = (defs.structs.iter().enumerate())
        .map(|(s, fields)| {
            let each = (fields.iter().enumerate())
                .map(|(f, t)| {
                    let default = matches!(t, Ty::Bool | Ty::Int(_) | Ty::Text) && g.one_in(3);
                    (about(g, &format!("S{s}.f{f}")), default)
                })
                .collect();
            (about(g, &format!("S{s}")), each)
        })
        .collect();
    let enums = (defs.enums.iter().enumerate())
        .map(|(e, cases)| {
            let each = (0..cases.len())
                .map(|c| about(g, &format!("E{e}.c{c}")))
                .collect();
            (about(g, &format!("E{e}")), each)
        })
        .collect();
    Notes { structs, enums }
}

fn comment(about: &Option<String>) -> String {
    about.as_ref().map_or(String::new(), |a| format!("// {a}\n"))
}

/// The source of the structs and enums, with their comments and defaults.
fn source(defs: &Defs, notes: &Notes) -> String {
    let mut out = String::new();
    for (s, fields) in defs.structs.iter().enumerate() {
        let (about, each) = &notes.structs[s];
        out += &format!("{}struct S{s} {{\n", comment(about));
        for (f, t) in fields.iter().enumerate() {
            let (about, default) = &each[f];
            let tail = if !default {
                String::new()
            } else {
                match t {
                    Ty::Bool => " = false".to_string(),
                    Ty::Text => " = \"\"".to_string(),
                    _ => " = 0".to_string(),
                }
            };
            let tail = if *default && matches!(t, Ty::Int(n) if INTS.iter().any(|(m, least, _)| m == n && *least > 0)) {
                " = 0".to_string()
            } else {
                tail
            };
            out += &format!("{}  f{f}: {}{tail}\n", indent(about), defs.name(t));
        }
        out += "}\n\n";
    }
    for (e, cases) in defs.enums.iter().enumerate() {
        let (about, each) = &notes.enums[e];
        out += &format!("{}enum E{e} {{\n", comment(about));
        for (c, held) in cases.iter().enumerate() {
            let head = indent(&each[c]);
            match held {
                Some(t) => out += &format!("{head}  c{c}: {}\n", defs.name(t)),
                None => out += &format!("{head}  c{c}\n"),
            }
        }
        out += "}\n\n";
    }
    out
}

fn indent(about: &Option<String>) -> String {
    about.as_ref().map_or(String::new(), |a| format!("  // {a}\n"))
}

fn text_of(about: &Option<String>) -> &str {
    about.as_deref().unwrap_or("")
}

/// The calls the describer receives for the type.
fn calls(defs: &Defs, notes: &Notes, t: &Ty) -> Vec<String> {
    match t {
        Ty::Bool => vec!["boolean".to_string()],
        Ty::Int(name) => {
            let (_, least, greatest) = *INTS.iter().find(|(n, ..)| n == name).unwrap();
            let call = if greatest > i128::from(i64::MAX) {
                "unsigned"
            } else {
                "signed"
            };
            vec![format!("{call} {least} {greatest}")]
        }
        Ty::Text => vec!["text".to_string()],
        Ty::Unit => vec!["nothing".to_string()],
        Ty::Option(of) => [vec!["optional".to_string()], calls(defs, notes, of)].concat(),
        Ty::List(of) => [vec!["sequence".to_string()], calls(defs, notes, of)].concat(),
        Ty::Tuple(of) => {
            let mut out = vec![format!("tuple {}", of.len())];
            for t in of {
                out.extend(calls(defs, notes, t));
            }
            out
        }
        Ty::Struct(s) => {
            let (about, each) = &notes.structs[*s];
            let fields = &defs.structs[*s];
            let mut out = vec![format!("structure S{s} {} {}", text_of(about), fields.len())];
            for (f, t) in fields.iter().enumerate() {
                let (about, default) = &each[f];
                let may = *default || matches!(t, Ty::Option(_));
                out.push(format!("field f{f} {} {may}", text_of(about)));
                out.extend(calls(defs, notes, t));
            }
            out.push("end_structure".to_string());
            out
        }
        Ty::Enum(e) => {
            let (about, each) = &notes.enums[*e];
            let cases = &defs.enums[*e];
            let mut out = vec![format!("enumeration E{e} {} {}", text_of(about), cases.len())];
            for (c, held) in cases.iter().enumerate() {
                out.push(format!("case c{c} {} {}", text_of(&each[c]), held.is_some()));
                if let Some(t) = held {
                    out.extend(calls(defs, notes, t));
                }
            }
            out.push("end_enumeration".to_string());
            out
        }
        Ty::Own | Ty::Barred => unreachable!("not generated"),
    }
}

fn imports() -> Vec<(&'static str, &'static [&'static str])> {
    vec![("std.describe", &["Describe", "Describer"])]
}

fn case(g: &mut Gen) -> Case {
    let parts = Parts {
        lists: true,
        own: false,
        barred: false,
    };
    let mut defs = Defs::default();
    let top = ty(g, &mut defs, 3, parts);
    let notes = notes(g, &defs);
    let rest = format!(
        "{}{}fn main() ! console, alloc {{\n  d = Describe[{}].describe(Log(lines: \"\"))\n  print(d.lines)\n}}\n",
        log(),
        source(&defs, &notes),
        defs.name(&top)
    );
    Case {
        source: with_import(&imports(), &rest),
        calls: calls(&defs, &notes, &top),
    }
}

fn a_type_describes_the_calls_of_the_shapes_of_its_parts_in_order() {
    generate::check(5521, 18, case, |c| {
        let found = errors(&c.source);
        assert!(found.is_empty(), "{found:?}\n{}", c.source);
        let expected: String = c.calls.iter().map(|l| format!("{l}\n")).collect();
        // The describer's lines are told with the empty comment as two spaces.
        let said = prints(&c.source);
        assert_eq!(
            said.replace("  ", " "),
            format!("{}\n", expected.replace("  ", " ")),
            "{}",
            c.source
        );
    });
}

/// A range tells its least and its greatest, as a number of 64 bits when
/// they fit in one and as an unsigned one when they do not.
fn a_range_describes_its_least_and_its_greatest_value() {
    let ranges: &[(&str, &str)] = &[
        ("1..6", "signed 1 5"),
        ("0..1000", "signed 0 999"),
        ("-5..5", "signed -5 4"),
        ("0..256", "signed 0 255"),
        ("0..18446744073709551616", "unsigned 0 18446744073709551615"),
        ("9223372036854775808..18446744073709551616", "unsigned 9223372036854775808 18446744073709551615"),
    ];
    for (range, said) in ranges {
        let rest = format!(
            "{}struct P {{\n  a: {range}\n}}\n\nfn main() ! console, alloc {{\n  d = Describe[P].describe(Log(lines: \"\"))\n  print(d.lines)\n}}\n",
            log()
        );
        let source = with_import(&imports(), &rest);
        let found = errors(&source);
        assert!(found.is_empty(), "{found:?}\n{source}");
        assert_eq!(
            prints(&source),
            format!("structure P  1\nfield a  false\n{said}\nend_structure\n\n"),
            "{source}"
        );
    }
}

fn a_type_taken_from_without_describe_refuses_every_type_that_holds_it() {
    let rest = format!(
        "{}struct Session {{\n  token: str\n}}\n\nwithout Describe for Session\n\nstruct Login {{\n  by: Session\n}}\n\nfn main() ! console, alloc {{\n  d = Describe[Login].describe(Log(lines: \"\"))\n  print(d.lines)\n}}\n",
        log()
    );
    let source = with_import(&imports(), &rest);
    let found = errors(&source);
    assert_eq!(found.len(), 1, "{found:?}\n{source}");
    assert!(found[0].message.contains("`Describe`"), "{}", found[0]);
    assert!(found[0].message.contains("`Session`"), "{}", found[0]);
}

fn a_types_own_impl_is_the_description_used_wherever_it_is_a_part() {
    let rest = format!(
        "{}struct Celsius {{\n  degrees: Int\n}}\n\nimpl Describe[Celsius] {{\n  fn describe[D: Describer](to: take D) -> D ! alloc {{ Describer.text(to) }}\n}}\n\nstruct Reading {{\n  at: Celsius\n  past: List[Celsius]\n}}\n\nfn main() ! console, alloc {{\n  d = Describe[Reading].describe(Log(lines: \"\"))\n  print(d.lines)\n}}\n",
        log()
    );
    let source = with_import(&imports(), &rest);
    let found = errors(&source);
    assert!(found.is_empty(), "{found:?}\n{source}");
    assert_eq!(
        prints(&source),
        "structure Reading  2\nfield at  false\ntext\nfield past  false\nsequence\ntext\nend_structure\n\n",
        "{source}"
    );
}

/// The comments are read from the source only for a program that asks for a
/// description, so a program that does not has the executable it had with no
/// comment written, and one that does has a different one.
fn a_program_that_describes_nothing_holds_no_comment() {
    let built = |comments: bool, describes: bool| {
        let (about, title) = if comments {
            ("// A note.\n", "  // The title.\n")
        } else {
            ("", "")
        };
        let rest = if describes {
            format!(
                "{}{about}struct Note {{\n{title}  title: str\n}}\n\nfn main() ! console, alloc {{\n  d = Describe[Note].describe(Log(lines: \"\"))\n  print(d.lines)\n}}\n",
                log()
            )
        } else {
            format!("{about}struct Note {{\n{title}  title: str\n}}\n\nfn main() ! console {{\n  print(\"x\")\n}}\n")
        };
        let source = with_import(&imports(), &rest);
        // The constants of the program, which hold what a comment says, and not
        // where the code is, which a comment moves.
        let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{source}"));
        format!("{:?}", program.data)
    };
    assert_eq!(built(true, false), built(false, false));
    assert_ne!(built(true, true), built(false, true));
}

fn every_executable_of_a_description_prints_what_the_evaluator_says() {
    generate::check(5523, 4, case, |c| {
        let program = lower(c.source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{}", c.source));
        let said = scalar::run(&program);
        for target in targets::runnable() {
            targets::runs_as(c.source.as_bytes(), target, &said);
        }
    });
}

/// `Args`, the struct that `Function` gives for a function, is described with
/// the comment on the function and on each parameter, and a parameter with a
/// default may be left out.
fn the_arguments_of_a_function_are_described_with_the_comments_on_it_and_on_each_parameter() {
    let rest = format!(
        "{}// Adds a number.\nfn add(\n  // The first.\n  a: Int,\n  b: Int = 2\n) -> Int {{ a }}\n\nfn tell[F, A, R](f: F) -> str ! alloc\n  requires Function[F, A, R]\n  requires Describe[A]\n{{\n  d = Describe[A].describe(Log(lines: \"\"))\n  d.lines\n}}\n\nfn main() ! console, alloc {{\n  print(tell(add))\n}}\n",
        log()
    );
    let source = with_import(&imports(), &rest);
    let found = errors(&source);
    assert!(found.is_empty(), "{found:?}\n{source}");
    let said = prints(&source);
    let lines: Vec<&str> = said.lines().collect();
    assert_eq!(lines[0], "structure add.Args Adds a number. 2", "{said}");
    assert_eq!(lines[1], "field a The first. false", "{said}");
    assert_eq!(lines[3], "field b  true", "{said}");
}
