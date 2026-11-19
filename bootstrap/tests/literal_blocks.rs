//! A whole number written at the end of a block of an `if` or a `match` that
//! gives a value takes the type of the other blocks, or of the place the
//! value is used in.

use smith::diag::Pos;
use smith::lower;
use smith::scalar;

use crate::Test;
use crate::generate::{self, Gen};
use crate::lists::header;

/// The whole number types, with their names, smallest and largest values.
const INTS: [(&str, i128, i128); 8] = [
    ("i8", i8::MIN as i128, i8::MAX as i128),
    ("i16", i16::MIN as i128, i16::MAX as i128),
    ("i32", i32::MIN as i128, i32::MAX as i128),
    ("Int", i64::MIN as i128, i64::MAX as i128),
    ("Byte", 0, u8::MAX as i128),
    ("u16", 0, u16::MAX as i128),
    ("u32", 0, u32::MAX as i128),
    ("u64", 0, u64::MAX as i128),
];

pub const ONCE: &[Test] = &[
    (
        "a_literal_in_a_block_takes_the_type_of_the_other_blocks_and_of_its_place",
        a_literal_in_a_block_takes_the_type_of_the_other_blocks_and_of_its_place,
    ),
    (
        "a_literal_a_block_cannot_hold_is_refused_at_the_literal",
        a_literal_a_block_cannot_hold_is_refused_at_the_literal,
    ),
    (
        "blocks_of_two_known_types_give_the_smallest_range_that_holds_both_and_are_refused_when_none_does",
        blocks_of_two_known_types_give_the_smallest_range_that_holds_both_and_are_refused_when_none_does,
    ),
    (
        "any_blocks_put_anywhere_never_panic_the_compiler",
        any_blocks_put_anywhere_never_panic_the_compiler,
    ),
];

/// How a block ends.
#[derive(Clone, Copy, Debug)]
enum End {
    /// A whole number, written out.
    Literal(i128),
    /// A name of the type.
    Name,
    /// A call of a function that gives back what it is given.
    Call,
}

/// Where the value of the `if` or the `match` is used.
#[derive(Clone, Copy, Debug)]
enum Place {
    /// A binding that states the type.
    Binding,
    /// A binding with no type, whose use needs the type.
    Bare,
    /// An argument of a function that takes the type.
    Argument,
    /// A side of an operator, and the other side has the type.
    Operand { op: usize, left: bool },
    /// A field of that type.
    Field,
    /// The value a function that gives the type returns.
    Returned,
}

#[derive(Clone, Debug)]
struct Blocks {
    int: usize,
    /// Whether it is a `match`.
    matching: bool,
    ends: Vec<End>,
    /// Whether the block has a binding before its end.
    busy: Vec<bool>,
    place: Place,
    /// The block that runs.
    runs: usize,
    /// The value of the name.
    held: i128,
}

/// The operators and functions that take two values of one type.
const OPERATORS: [&str; 12] = [
    "==",
    "!=",
    "<",
    "<=",
    ">",
    ">=",
    "+",
    "-",
    "*",
    "wrapping_add",
    "wrapping_sub",
    "wrapping_mul",
];

/// Whether the result of the operator must be proven to fit, which may
/// refuse a program that is no mistake of the type.
fn proven(op: usize) -> bool {
    (6..9).contains(&op)
}

fn value(g: &mut Gen, low: i128, high: i128) -> i128 {
    match g.below(6) {
        0 => low,
        1 => high,
        2 => 0,
        3 => 1.clamp(low, high),
        4 => (-1).clamp(low, high),
        _ => low + (g.below(1 << 20) as i128 * (high - low) / (1 << 20)),
    }
}

/// The blocks of an `if` or a `match` whose literals fit the type, in which
/// either every block ends in a literal, or some do and some do not.
fn blocks(g: &mut Gen) -> Blocks {
    let int = g.below(INTS.len());
    let (_, low, high) = INTS[int];
    let matching = g.one_in(2);
    let count = 2 + g.below(3);
    let count = if matching { count } else { count.min(3) };
    let all = g.one_in(4);
    let mut ends: Vec<End> = (0..count)
        .map(|_| match (all, g.below(3)) {
            (true, _) | (_, 0 | 1) => End::Literal(value(g, low, high)),
            (_, 2) if g.one_in(2) => End::Call,
            _ => End::Name,
        })
        .collect();
    if !all {
        // At least one block ends in a value, and one in a literal.
        let named = g.below(count);
        ends[named] = if g.one_in(3) { End::Call } else { End::Name };
        let other = (named + 1 + g.below(count - 1)) % count;
        ends[other] = End::Literal(value(g, low, high));
    }
    let place = match g.below(6) {
        0 => Place::Binding,
        1 => Place::Bare,
        2 => Place::Argument,
        3 => Place::Operand {
            op: g.below(OPERATORS.len()),
            left: g.one_in(2),
        },
        4 => Place::Field,
        _ => Place::Returned,
    };
    Blocks {
        int,
        matching,
        busy: (0..count).map(|_| g.one_in(4)).collect(),
        ends,
        place,
        runs: g.below(count),
        held: value(g, low, high),
    }
}

impl Blocks {
    /// The `if` or the `match`, with each literal written out, or in place
    /// of it a name `l0`, `l1`, and so on, that is bound to it before.
    fn conditional(&self, literals: bool) -> String {
        let ty = INTS[self.int].0;
        let ends: Vec<String> = self
            .ends
            .iter()
            .enumerate()
            .map(|(i, end)| {
                let end = match end {
                    End::Literal(n) if literals => n.to_string(),
                    End::Literal(_) => format!("l{i}"),
                    End::Name => "k".to_string(),
                    End::Call => "same(k)".to_string(),
                };
                match self.busy[i] {
                    true => format!("{{\n      b{i}: {ty} = 0\n      {end}\n    }}"),
                    false => format!("{{ {end} }}"),
                }
            })
            .collect();
        let n = ends.len();
        if self.matching {
            let mut text = String::from("match s {\n");
            for (i, end) in ends.iter().enumerate() {
                let pattern = if i + 1 == n { "_".to_string() } else { i.to_string() };
                text.push_str(&format!("    {pattern} {end}\n"));
            }
            text.push_str("  }");
            return text;
        }
        let mut text = String::new();
        for (i, end) in ends.iter().enumerate() {
            match i {
                0 => text.push_str(&format!("if s == 0 {end}")),
                _ if i + 1 == n => text.push_str(&format!(" else {end}")),
                _ => text.push_str(&format!(" else if s == {i} {end}")),
            }
        }
        text
    }

    /// The program, with the literals written out, or bound to names of the
    /// type in front of the `if` or the `match`.
    fn program(&self, literals: bool) -> String {
        let ty = INTS[self.int].0;
        let selector = self.runs.min(self.ends.len() - 1);
        let mut binds = String::new();
        if !literals {
            for (i, end) in self.ends.iter().enumerate() {
                if let End::Literal(n) = end {
                    binds.push_str(&format!("  l{i}: {ty} = {n}\n"));
                }
            }
        }
        let c = self.conditional(literals);
        let held = self.held;
        let mut items = format!("fn same(n: {ty}) -> {ty} {{ n }}\n\n");
        let setup = format!("  s: Byte = {selector}\n  k: {ty} = {held}\n{binds}");
        let main = match self.place {
            Place::Binding => format!("{setup}  r: {ty} = {c}\n  print(\"{{r}}\")\n"),
            Place::Bare => {
                items.push_str(&format!("fn takes(n: {ty}) -> {ty} {{ n }}\n\n"));
                format!("{setup}  r = {c}\n  print(\"{{takes(r)}}\")\n")
            }
            Place::Argument => {
                items.push_str(&format!("fn takes(n: {ty}) -> {ty} {{ n }}\n\n"));
                format!("{setup}  r = takes({c})\n  print(\"{{r}}\")\n")
            }
            Place::Operand { op, left } => {
                let op = OPERATORS[op];
                let c = format!("({c})");
                let (l, r) = match left {
                    true => (c.as_str(), "k"),
                    false => ("k", c.as_str()),
                };
                let used = match op.starts_with("wrapping") {
                    true => format!("{op}({l}, {r})"),
                    false => format!("({l} {op} {r})"),
                };
                format!("{setup}  r = {used}\n  print(\"{{r}}\")\n")
            }
            Place::Field => {
                items.push_str(&format!("struct Holder {{ f: {ty} }}\n\n"));
                format!("{setup}  h = Holder(f: {c})\n  print(\"{{h.f}}\")\n")
            }
            Place::Returned => {
                items.push_str(&format!(
                    "fn back(s: Byte, k: {ty}) -> {ty} {{\n{binds}  {c}\n}}\n\n"
                ));
                format!("  s: Byte = {selector}\n  k: {ty} = {held}\n  print(\"{{back(s, k)}}\")\n")
            }
        };
        let int: Vec<&str> = match self.place {
            Place::Operand { op, .. } if OPERATORS[op].starts_with("wrapping") => vec![OPERATORS[op]],
            _ => vec![],
        };
        let uses = header(
            &[("std.console", &["console", "print"]), ("std.int", &int[..])],
            ty,
        );
        format!("{uses}{items}fn main() ! console, alloc {{\n{main}}}\n")
    }

    /// Whether the place may refuse a program for a result it cannot prove
    /// to fit, which is no mistake of the type.
    fn is_proven(&self) -> bool {
        matches!(self.place, Place::Operand { op, .. } if proven(op))
    }
}

fn a_literal_in_a_block_takes_the_type_of_the_other_blocks_and_of_its_place() {
    // The program of the issue.
    let issue = "use std.console (console, print)\nuse std.list (push)\n\nfn main() ! console, alloc {\n  e: Byte = 110\n  var value: List[Byte] = []\n  \
        decoded = if e == 'n' { '\\n' } else { e }\n  value = push(value, decoded)\n  \
        print(\"{value.size}\")\n}\n";
    let program = lower(issue.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
    assert_eq!(scalar::run(&program).stdout, b"1\n");

    generate::check(2210, 60, blocks, |case| {
        // What a program does is what the program does with a name of the
        // type in place of each literal.
        let results: Vec<_> = [true, false]
            .map(|literals| match lower(case.program(literals).as_bytes()) {
                Ok(program) => Ok(scalar::run(&program).stdout),
                Err(found) => Err(found),
            })
            .into();
        let source = case.program(true);
        match (&results[0], &results[1]) {
            (Ok(a), Ok(b)) => assert_eq!(a, b, "{source}"),
            // The proof that a result fits is of the program as written, so
            // it may refuse one of them, and then only for that.
            (a, b) if case.is_proven() => {
                for found in [a, b].into_iter().filter_map(|r| r.as_ref().err()) {
                    assert!(
                        found.iter().all(|e| e.message.contains("cannot prove")),
                        "{found:?}\n{source}"
                    );
                }
            }
            (a, b) => panic!("{a:?} {b:?}\n{source}"),
        }
    });
}

/// The place of `needle`, which is written once, in `source`.
fn place_of(source: &str, needle: &str) -> Pos {
    let at = source.find(needle).unwrap();
    let line = source[..at].matches('\n').count() + 1;
    let col = at - source[..at].rfind('\n').map_or(0, |n| n + 1) + 1;
    Pos { line, col }
}

fn a_literal_a_block_cannot_hold_is_refused_at_the_literal() {
    generate::check(2211, 60, blocks, |case| {
        let (ty, low, high) = INTS[case.int];
        let mut case = case.clone();
        // One literal, which no type holds.
        let at = (0..case.ends.len())
            .find(|i| matches!(case.ends[*i], End::Literal(_)))
            .unwrap();
        case.ends[at] = End::Literal(0);
        let number = match case.held % 2 {
            0 => high + 1,
            _ => low - 1,
        }
        .to_string();
        let marked = case.program(true).replacen(" 0 }", " @@ }", 1);
        let marked = match marked.contains("@@") {
            true => marked,
            // The block has a binding before it, and the literal is alone on
            // its line.
            false => case.program(true).replacen("\n      0\n", "\n      @@\n", 1),
        };
        let place = place_of(&marked, "@@");
        let source = marked.replace("@@", &number);
        let Err(found) = lower(source.as_bytes()) else {
            panic!("builds\n{source}");
        };
        let here: Vec<_> = found.iter().filter(|e| e.pos == place).collect();
        assert_eq!(here.len(), 1, "{found:?}\n{source}");
        assert!(case.is_proven() || found.len() == 1, "{found:?}\n{source}");
        for name in [ty, &low.to_string(), &high.to_string()] {
            assert!(here[0].message.contains(name), "{}", here[0]);
        }
    });
}

fn blocks_of_two_known_types_give_the_smallest_range_that_holds_both_and_are_refused_when_none_does() {
    generate::check(2212, 40, blocks, |case| {
        let (first, second) = (INTS[case.int].0, INTS[(case.int + 1 + case.held.rem_euclid(7) as usize) % 8].0);
        let n = case.ends.len();
        // Every block ends in a name of one of two types, and some in a
        // literal.
        let ends: Vec<String> = (0..n)
            .map(|i| match (i, &case.ends[i]) {
                (0, _) => "{ a }".to_string(),
                (1, _) => "{ b }".to_string(),
                (_, End::Literal(_)) => "{ 1 }".to_string(),
                _ => "{ a }".to_string(),
            })
            .collect();
        let mut text = String::new();
        for (i, end) in ends.iter().enumerate() {
            match i {
                0 => text.push_str(&format!("if s == 0 {end}")),
                _ if i + 1 == n => text.push_str(&format!(" else {end}")),
                _ => text.push_str(&format!(" else if s == {i} {end}")),
            }
        }
        let uses = header(&[("std.console", &["console", "print"])], &format!("{first} {second}"));
        let source = format!(
            "{uses}fn main() ! console, alloc {{\n  s: Byte = 0\n  a: {first} = 1\n  b: {second} = 1\n  r = {text}\n  print(\"{{r}}\")\n}}\n"
        );
        // The smallest range that holds both is a type when its numbers fit 64
        // bits, signed when it has a negative one.
        let other = INTS[(case.int + 1 + case.held.rem_euclid(7) as usize) % 8];
        let (low, high) = (INTS[case.int].1.min(other.1), INTS[case.int].2.max(other.2));
        let fits = low >= i64::MIN as i128
            && high <= if low < 0 { i64::MAX as i128 } else { u64::MAX as i128 };
        match lower(source.as_bytes()) {
            Ok(program) => {
                assert!(fits, "builds\n{source}");
                assert_eq!(scalar::run(&program).stdout, b"1\n", "{source}");
            }
            Err(found) => {
                assert!(!fits && !found.is_empty(), "{found:?}\n{source}");
                for e in &found {
                    assert!(
                        e.message.contains(first) || e.message.contains(second),
                        "{}\n{source}",
                        e
                    );
                }
            }
        }
    });
}

/// What a block may end in, which may be no number.
const ENDS: [&str; 7] = ["7", "-3", "'a'", "\"s\"", "true", "k", "same(k)"];

/// Where a value may be used.
const PLACES: [&str; 8] = [
    "{}",
    "x: Byte = {}",
    "x: str = {}",
    "takes({})",
    "x == {}",
    "{} == x",
    "wrapping_add(x, {})",
    "(a, b): (Byte, bool) = {}",
];

fn anything(g: &mut Gen) -> String {
    let count = 2 + g.below(3);
    let mut text = String::new();
    for i in 0..count {
        let end = ENDS[g.below(ENDS.len())];
        let pattern = if i + 1 == count { "_".to_string() } else { i.to_string() };
        text.push_str(&match g.one_in(2) {
            true => format!("    {pattern} {{ {end} }}\n"),
            false => format!("    {pattern} {{\n      return {end}\n    }}\n"),
        });
    }
    let value = match g.one_in(2) {
        true => format!("match s {{\n{text}  }}"),
        false => {
            let ends: Vec<&str> = (0..2).map(|_| ENDS[g.below(ENDS.len())]).collect();
            format!("if s == 0 {{ {} }} else {{ {} }}", ends[0], ends[1])
        }
    };
    let place = PLACES[g.below(PLACES.len())].replace("{}", &value);
    let int: &[&str] = if place.contains("wrapping_add") { &["wrapping_add"] } else { &[] };
    let uses = header(&[("std.console", &["console"]), ("std.int", int)], "");
    format!(
        "{uses}fn same(n: Byte) -> Byte {{ n }}\n\nfn takes(n: Byte) -> Byte {{ n }}\n\n\
         fn main() ! console, alloc {{\n  s: Byte = 1\n  x: Byte = 1\n  k: Byte = 2\n  {place}\n}}\n"
    )
}

fn any_blocks_put_anywhere_never_panic_the_compiler() {
    generate::check(2213, 60, anything, |source| {
        // The compiler gives an error or a program, and does not stop.
        let _ = lower(source.as_bytes());
    });
}
