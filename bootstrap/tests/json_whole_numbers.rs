//! `from_json` reads a whole number of any number of digits as the value it
//! has when the type holds it, and refuses it when the type does not.
//!
//! The numbers lie at and next to each power of ten, and each edge of
//! `u64` and `Int`, which are where a count of digits changes how a number
//! is read.

use smith::{lower, scalar};

use crate::Test;

pub const ONCE: &[Test] = &[(
    "a_whole_number_of_any_digits_is_read_as_its_value_when_the_type_holds_it",
    a_whole_number_of_any_digits_is_read_as_its_value_when_the_type_holds_it,
)];

fn numbers() -> Vec<i128> {
    let mut edges = vec![0i128, 1, 9, 10, 99];
    for k in 2..=24u32 {
        let p = 10i128.pow(k);
        edges.extend([p - 1, p, p + 1]);
    }
    for edge in [u64::MAX as i128, i64::MAX as i128, i64::MIN as i128] {
        edges.extend([edge - 1, edge, edge + 1]);
    }
    let negatives: Vec<i128> = edges.iter().map(|n| -n).collect();
    edges.extend(negatives);
    edges.sort();
    edges.dedup();
    edges
}

/// For each number, `show_u` and `show_i` print what `from_json` gives a
/// `u64` and an `Int`: `ok` and the number, or `err`.
fn a_whole_number_of_any_digits_is_read_as_its_value_when_the_type_holds_it() {
    let all = numbers();
    let mut calls = String::new();
    let mut expected = String::new();
    for n in &all {
        calls += &format!("  show_u(\"{n}\")\n  show_i(\"{n}\")\n");
        let line = |holds: bool| match holds {
            true => format!("ok {n}\n"),
            false => "err\n".to_string(),
        };
        expected += &line((0..=u64::MAX as i128).contains(n));
        expected += &line((i64::MIN as i128..=i64::MAX as i128).contains(n));
    }
    let source = format!(
        "use std.bits (u64)\nuse std.console (console, print)\nuse std.deserialize (ReadError)\nuse std.json (from_json)\n\nfn show_u(s: str) ! console, alloc {{\n  r: Result[u64, ReadError] = from_json(s)\n  match r {{\n    .ok(v) {{ print(\"ok {{v}}\") }}\n    .err(e) {{ print(\"err\") }}\n  }}\n}}\n\nfn show_i(s: str) ! console, alloc {{\n  r: Result[Int, ReadError] = from_json(s)\n  match r {{\n    .ok(v) {{ print(\"ok {{v}}\") }}\n    .err(e) {{ print(\"err\") }}\n  }}\n}}\n\nfn main() ! console, alloc {{\n{calls}}}\n"
    );
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
    assert_eq!(String::from_utf8(scalar::run(&program).stdout).unwrap(), expected);
}
