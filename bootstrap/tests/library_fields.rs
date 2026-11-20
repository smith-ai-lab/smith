//! A field of a struct of the library that is not `pub` is private to its
//! module, as one of a project's module is. The structs and their fields are
//! read from `std/`, so a struct that is added is covered by every property
//! here.

use std::path::Path;
use std::sync::OnceLock;

use smith::check;

use crate::Test;
use crate::generate;
use crate::programs::pos_after;

pub const ONCE: &[Test] = &[
    (
        "library_fields_a_private_field_is_refused_when_built_or_read_at_its_place",
        library_fields_a_private_field_is_refused_when_built_or_read_at_its_place,
    ),
    (
        "library_fields_a_program_that_only_holds_the_struct_is_accepted",
        library_fields_a_program_that_only_holds_the_struct_is_accepted,
    ),
    (
        "library_fields_the_fix_for_a_private_field_names_what_the_module_gives_in_its_place",
        library_fields_the_fix_for_a_private_field_names_what_the_module_gives_in_its_place,
    ),
    (
        "library_fields_no_program_makes_a_rawjson_from_text_that_is_not_json",
        library_fields_no_program_makes_a_rawjson_from_text_that_is_not_json,
    ),
];

/// A `pub struct` of a module of `std/` that has a field without `pub`.
#[derive(Debug)]
struct Hidden {
    module: String,
    name: String,
    /// Each field: whether it is `pub`, its name, and its type.
    fields: Vec<(bool, String, String)>,
}

fn hidden() -> &'static [Hidden] {
    static ALL: OnceLock<Vec<Hidden>> = OnceLock::new();
    ALL.get_or_init(|| {
        let dir = Path::new(env!("CARGO_MANIFEST_DIR")).join("../std");
        let mut files: Vec<_> = std::fs::read_dir(dir)
            .unwrap()
            .map(|e| e.unwrap().path())
            .filter(|p| p.extension().is_some_and(|e| e == "smith"))
            .filter(|p| p.file_stem().is_some_and(|s| s != "prelude"))
            .collect();
        files.sort();
        let mut found = Vec::new();
        for file in files {
            let module = file.file_stem().unwrap().to_str().unwrap().to_string();
            let text = std::fs::read_to_string(&file).unwrap();
            let mut lines = text.lines();
            while let Some(line) = lines.next() {
                let Some(head) = line.strip_prefix("pub struct ") else {
                    continue;
                };
                let name: String = head
                    .chars()
                    .take_while(|c| c.is_alphanumeric() || *c == '_')
                    .collect();
                let mut fields = Vec::new();
                for line in lines.by_ref().take_while(|l| !l.starts_with('}')) {
                    let line = line.trim();
                    let (public, line) = match line.strip_prefix("pub ") {
                        Some(rest) => (true, rest),
                        None => (false, line),
                    };
                    if let Some((field, ty)) = line.split_once(": ") {
                        fields.push((public, field.to_string(), ty.trim().to_string()));
                    }
                }
                if fields.iter().any(|(public, ..)| !public) {
                    found.push(Hidden {
                        module: module.clone(),
                        name,
                        fields,
                    });
                }
            }
        }
        assert!(!found.is_empty(), "no struct of std/ has a private field");
        found
    })
}

/// A value of a type a field has, written as a literal, when a program can
/// write one. A struct of the library that holds another struct of it is
/// built by no program, so its fields are read and not given.
fn literal(ty: &str) -> Option<&'static str> {
    match ty {
        "str" => Some("\"a\""),
        "Nat" => Some("0"),
        "bool" => Some("true"),
        "List[Byte]" => Some("[]"),
        _ => None,
    }
}

#[derive(Debug)]
struct Case {
    which: usize,
    /// The private field that is read.
    field: usize,
    build: bool,
    pad: usize,
}

fn case(g: &mut generate::Gen) -> Case {
    let all = hidden();
    let which = g.below(all.len());
    let private: Vec<usize> = (0..all[which].fields.len())
        .filter(|&i| !all[which].fields[i].0)
        .collect();
    Case {
        which,
        field: private[g.below(private.len())],
        build: all[which].fields.iter().all(|(_, _, t)| literal(t).is_some()) && g.one_in(2),
        pad: g.below(4),
    }
}

/// The `use` line that brings in a struct, and a `main` that reaches the
/// function `f`, which takes the struct or makes it, so that the checker reads
/// `f`. A struct that no function of its module gives needs a line here.
fn around(h: &Hidden, takes: bool) -> (String, String) {
    let (names, takes_main) = match h.name.as_str() {
        "RawJson" => (
            "RawJson, raw_json",
            "fn main() ! alloc {
  match raw_json(\"[]\") {
    .ok(x) { f(x) }
    .err(e) { }
  }
}
",
        ),
        "Html" => ("Html, render", "fn main() ! alloc {
  f(render() { })
}
"),
        "Text" => ("Text, text", "fn main() {
  f(text())
}
"),
        "Decoder" => ("Decoder, decoder", "fn main() ! alloc {
  f(decoder())
}
"),
        "Encoder" => ("Encoder, encoder", "fn main() ! alloc {
  f(encoder())
}
"),
        other => panic!("the test makes no `{other}`, so it needs a way to"),
    };
    let main = if takes {
        takes_main.to_string()
    } else {
        "fn main() {\n  x = f()\n}\n".to_string()
    };
    (format!("use std.{} ({names})\n\n", h.module), main)
}

fn library_fields_a_private_field_is_refused_when_built_or_read_at_its_place() {
    generate::check(1, 20, case, |c| {
        let h = &hidden()[c.which];
        let (_, field, _) = &h.fields[c.field];
        let (uses, main) = around(h, !c.build);
        let before = format!("{uses}{}", "\n".repeat(c.pad));
        let (source, at) = if c.build {
            let given: Vec<String> = h
                .fields
                .iter()
                .map(|(_, f, t)| format!("{f}: {}", literal(t).expect("a literal")))
                .collect();
            let start = format!("{before}fn f() -> {} {{\n  ", h.name);
            let source = format!("{start}{}({})\n}}\n\n{main}", h.name, given.join(", "));
            (source, pos_after(&start))
        } else {
            let start = format!("{before}fn f(s: {}) {{\n  x = s.", h.name);
            (format!("{start}{field}\n}}\n\n{main}"), pos_after(&start))
        };
        let Err(errors) = check(source.as_bytes()) else {
            panic!("accepted:\n{source}");
        };
        assert_eq!(errors.len(), 1, "{errors:?} for\n{source}");
        let e = &errors[0];
        assert_eq!(e.pos, at, "{e:?} for\n{source}");
        assert!(e.message.contains(&h.name), "{e}");
        assert!(e.message.contains("private"), "{e}");
        if !c.build {
            assert!(e.message.contains(field.as_str()), "{e}");
        }
        // A program cannot change the library, so the fix never says to.
        let fix = e.fix.as_deref().unwrap_or_default();
        assert!(!fix.contains("write `pub`"), "{fix} for\n{source}");
    });
}

fn library_fields_a_program_that_only_holds_the_struct_is_accepted() {
    generate::check(2, 20, case, |c| {
        let h = &hidden()[c.which];
        let (uses, main) = around(h, true);
        let source = format!(
            "{uses}{}fn f(s: {}) {{\n  t = s\n}}\n\n{main}",
            "\n".repeat(c.pad),
            h.name
        );
        if let Err(errors) = check(source.as_bytes()) {
            panic!("{errors:?} for\n{source}");
        }
    });
}

/// A `RawJson` holds one whole JSON value. A program cannot build one from a
/// text, and the text it gets from `raw_json` is checked, so what is written
/// is JSON.
fn library_fields_no_program_makes_a_rawjson_from_text_that_is_not_json() {
    let source = "use std.json (RawJson)\n\nfn bad() -> RawJson {\n  RawJson(text: \"] not json\")\n}\n\nfn main() {\n  x = bad()\n}\n";
    let Err(errors) = check(source.as_bytes()) else {
        panic!("accepted");
    };
    assert_eq!(errors.len(), 1, "{errors:?}");
    assert!(errors[0].message.contains("RawJson"), "{}", errors[0]);
}

/// The fix for the field of a `RawJson` names the function that gives it.
fn library_fields_the_fix_for_a_private_field_names_what_the_module_gives_in_its_place() {
    let source = "use std.json (RawJson, raw_json)\n\nfn f(s: RawJson) {\n  x = s.text\n}\n\nfn main() ! alloc {\n  match raw_json(\"[]\") {\n    .ok(x) { f(x) }\n    .err(e) { }\n  }\n}\n";
    let Err(errors) = check(source.as_bytes()) else {
        panic!("accepted");
    };
    assert_eq!(errors.len(), 1, "{errors:?}");
    let fix = errors[0].fix.as_deref().unwrap_or_default();
    assert!(fix.contains("raw_text"), "{fix}");
}
