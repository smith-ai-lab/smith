//! `append` of `std.list` adds the elements of one list after another's, with
//! one test for room and one copy.
//!
//! The generator keeps a model of the lists it builds in plain vectors, so it
//! shares nothing with the compiler.

use smith::scalar;
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::growth;
use crate::lists::header;
use crate::numbers::{INTS, Int, value};

pub const ONCE: &[Test] = &[
    (
        "appending_a_list_gives_the_elements_of_both_in_order_and_leaves_the_list_added_unchanged",
        appending_a_list_gives_the_elements_of_both_in_order_and_leaves_the_list_added_unchanged,
    ),
    (
        "appending_a_text_adds_its_bytes",
        appending_a_text_adds_its_bytes,
    ),
    (
        "a_text_of_any_length_is_added_whole_after_what_the_list_holds",
        a_text_of_any_length_is_added_whole_after_what_the_list_holds,
    ),
    (
        "appending_asks_for_memory_once_however_many_elements_it_adds",
        appending_asks_for_memory_once_however_many_elements_it_adds,
    ),
    (
        "the_list_appended_to_is_given_away_and_the_list_appended_is_lent",
        the_list_appended_to_is_given_away_and_the_list_appended_is_lent,
    ),
    (
        "append_is_refused_for_elements_that_cannot_be_copied_for_a_slice_and_without_alloc",
        append_is_refused_for_elements_that_cannot_be_copied_for_a_slice_and_without_alloc,
    ),
    (
        "a_function_of_the_program_named_append_is_called_as_written",
        a_function_of_the_program_named_append_is_called_as_written,
    ),
];

fn refused(source: &str) -> Vec<String> {
    match check(source.as_bytes()) {
        Ok(_) => Vec::new(),
        Err(errors) => errors.into_iter().map(|e| e.message).collect(),
    }
}

fn prints(source: &str) -> String {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    String::from_utf8(scalar::run(&program).stdout).unwrap()
}

#[derive(Debug)]
struct Joined {
    ty: Int,
    /// What each list is pushed before, which is what leaves it room.
    first: Vec<i128>,
    second: Vec<i128>,
}

fn joined(g: &mut Gen) -> Joined {
    let ty = *g.pick(INTS);
    let first = (0..g.below(20)).map(|_| value(g, ty)).collect();
    let second = (0..g.below(20)).map(|_| value(g, ty)).collect();
    Joined { ty, first, second }
}

fn pushes(name: &str, values: &[i128]) -> String {
    values.iter().map(|v| format!("  {name} = push({name}, {v})\n")).collect()
}

/// For every pair of lists of any whole number type, with any room in the
/// first, `append` gives the elements of the first and then those of the
/// second, and the second is as it was.
fn appending_a_list_gives_the_elements_of_both_in_order_and_leaves_the_list_added_unchanged() {
    generate::check(11, 40, joined, |j| {
        let t = j.ty.name;
        let body = format!(
            "fn main() ! console, alloc {{\n  var a: List[{t}] = []\n  var b: List[{t}] = []\n{}{}  a = append(a, b)\n  loop v in a {{ print(\"{{v}}\") }}\n  loop v in b {{ print(\"{{v}}\") }}\n}}\n",
            pushes("a", &j.first),
            pushes("b", &j.second),
        );
        let source = header(&[("std.console", &["console", "print"]), ("std.list", &["append", "push"])], &body) + &body;
        let both = j.first.iter().chain(&j.second).chain(&j.second);
        let expected: String = both.map(|v| format!("{v}\n")).collect();
        assert_eq!(prints(&source), expected, "for {source}");
    });
}

/// A text is added as its bytes, and is the same text after.
fn appending_a_text_adds_its_bytes() {
    generate::check(12, 30, |g| (0..g.below(12)).map(|_| *g.pick(&['a', 'z', ' ', 'é', '語', '😀'])).collect::<String>(), |s: &String| {
        let source = format!(
            "use std.console (console, print)\nuse std.list (append)\n\nfn main() ! console, alloc {{\n  var xs: List[Byte] = []\n  xs = append(xs, \"{s}\")\n  xs = append(xs, \"{s}\")\n  loop v in xs {{ print(\"{{v}}\") }}\n}}\n"
        );
        let expected: String = s.bytes().chain(s.bytes()).map(|b| format!("{b}\n")).collect();
        assert_eq!(prints(&source), expected, "for {source}");
    });
}

/// A text of each length from 0 to 40 bytes, each made of bytes of its own
/// to tell a byte left out or moved, is added after the ones before it, in a
/// list that has room for some and not for others.
fn a_text_of_any_length_is_added_whole_after_what_the_list_holds() {
    let mut body = String::new();
    let mut expected = String::new();
    let mut total = 0;
    for n in 0..=40usize {
        let text: String = (0..n).map(|k| char::from(b'a' + ((k * 7 + n) % 26) as u8)).collect();
        body += &format!("  xs = append(xs, \"{text}\")\n");
        for b in text.bytes() {
            expected += &format!("{b}\n");
        }
        total += n;
    }
    let source = format!(
        "use std.console (console, print)\nuse std.list (append)\n\nfn main() ! console, alloc {{\n  var xs: List[Byte] = []\n{body}  loop v in xs {{ print(\"{{v}}\") }}\n  print(\"{{xs.size}}\")\n}}\n"
    );
    assert_eq!(prints(&source), format!("{expected}{total}\n"), "for {source}");
}

/// An empty list given a text of any length asks for memory once, for room
/// for exactly its bytes, and is not asked for again.
fn appending_asks_for_memory_once_however_many_elements_it_adds() {
    for n in [1usize, 2, 3, 7, 8, 9, 16, 17, 100, 1000] {
        let text = "x".repeat(n);
        let source = format!(
            "use std.console (console, print)\nuse std.list (append)\n\nfn main() ! console, alloc {{\n  var xs: List[Byte] = []\n  xs = append(xs, \"{text}\")\n  print(\"{{xs.size}}\")\n}}\n"
        );
        let program = lower(source.as_bytes()).unwrap();
        let (outcome, taken) = scalar::run_taking(&program, usize::MAX);
        assert_eq!(outcome.stdout, format!("{n}\n").into_bytes());
        assert_eq!(growth::rooms(&taken)[..1], [n], "for {n} bytes: {taken:?}");
    }
}

const GIVE: &str = "use std.console (console, print)\nuse std.list (append)\n\nfn main() ! console, alloc {\n  var a: List[Byte] = []\n  var b: List[Byte] = []\n  a = append(a, b)\n";

fn the_list_appended_to_is_given_away_and_the_list_appended_is_lent() {
    let lent = format!("{GIVE}  print(\"{{a.size}} {{b.size}}\")\n}}\n");
    assert_eq!(refused(&lent), Vec::<String>::new(), "{lent}");
    let twice = "use std.console (console, print)\nuse std.list (append)\n\nfn main() ! console, alloc {\n  a: List[Byte] = []\n  b = append(a, a)\n}\n";
    let given = "use std.console (console, print)\nuse std.list (append)\n\nfn main() ! console, alloc {\n  a: List[Byte] = []\n  b: List[Byte] = []\n  c = append(a, b)\n  print(\"{a.size}\")\n}\n";
    for source in [twice, given] {
        let errors = refused(source);
        assert!(
            errors.iter().any(|e| e.contains("given away")),
            "{errors:?} does not say a list was given away for {source}"
        );
    }
}

fn append_is_refused_for_elements_that_cannot_be_copied_for_a_slice_and_without_alloc() {
    let lists = "use std.list (append)\n\nfn main() ! alloc {\n  var a: List[List[Byte]] = []\n  b: List[List[Byte]] = []\n  a = append(a, b)\n}\n";
    let errors = refused(lists);
    assert!(errors.iter().any(|e| e.contains("`append`") && e.contains("copied")), "{errors:?}");
    let slice = "use std.list (append, freeze)\n\nfn main() ! alloc {\n  s: Slice[Byte] = freeze([1])\n  a = append(s, [2])\n}\n";
    let errors = refused(slice);
    assert!(errors.iter().any(|e| e.contains("slice")), "{errors:?}");
    let none = "use std.list (append)\n\nfn main() {\n  a: List[Byte] = []\n  b: List[Byte] = []\n  c = append(a, b)\n}\n";
    let errors = refused(none);
    assert!(errors.iter().any(|e| e.contains("`alloc`")), "{errors:?}");
}

fn a_function_of_the_program_named_append_is_called_as_written() {
    let source = "use std.console (console, print)\n\nfn append(a: Byte, b: Byte) -> Byte { b }\n\nfn main() ! console, alloc {\n  print(\"{append(1, 2)}\")\n}\n";
    assert_eq!(prints(source), "2\n");
}
