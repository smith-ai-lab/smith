//! Properties of a `str` used as its bytes, and of `utf8`, which makes one
//! from bytes.
//!
//! Every generator builds its input from the bytes it means and keeps them, so
//! it shares nothing with the compiler. Validity is what the Unicode standard
//! says: the generator encodes characters, and breaks the bytes in the ways the
//! standard lists.

use smith::scalar::{self, Op, handler};
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "a_text_counts_the_bytes_of_its_utf8_and_is_read_by_a_byte_at_each_place",
        a_text_counts_the_bytes_of_its_utf8_and_is_read_by_a_byte_at_each_place,
    ),
    (
        "count_spaces_of_a_text_is_the_number_of_its_spaces",
        count_spaces_of_a_text_is_the_number_of_its_spaces,
    ),
    (
        "utf8_gives_the_text_of_valid_bytes_and_the_first_place_that_is_not",
        utf8_gives_the_text_of_valid_bytes_and_the_first_place_that_is_not,
    ),
    (
        "the_examples_of_the_page_on_text_hold",
        the_examples_of_the_page_on_text_hold,
    ),
    (
        "an_index_of_a_text_compiles_exactly_when_an_index_of_a_list_does_with_the_same_error",
        an_index_of_a_text_compiles_exactly_when_an_index_of_a_list_does_with_the_same_error,
    ),
    (
        "the_size_and_an_index_of_a_text_cost_what_they_cost_on_a_list",
        the_size_and_an_index_of_a_text_cost_what_they_cost_on_a_list,
    ),
    (
        "a_text_given_where_bytes_are_expected_is_neither_called_nor_copied",
        a_text_given_where_bytes_are_expected_is_neither_called_nor_copied,
    ),
    (
        "utf8_copies_nothing",
        utf8_copies_nothing,
    ),
];

/// What a program prints, which is what its text says.
fn prints(source: &str, lines: Vec<String>) {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let stdout: String = lines.iter().map(|l| format!("{l}\n")).collect();
    assert_eq!(
        scalar::run(&program),
        scalar::Outcome {
            stdout: stdout.into_bytes(),
            status: 0
        },
        "for {source}"
    );
}

/// The messages of the errors a program is refused with, or none if it
/// compiles.
fn refused(source: &str) -> Vec<String> {
    match check(source.as_bytes()) {
        Ok(_) => Vec::new(),
        Err(errors) => errors.into_iter().map(|e| e.message).collect(),
    }
}

/// Characters that a string literal holds as they are, of one to four bytes.
const CHARACTERS: &[char] = &[
    'a', 'b', 'Z', '0', ' ', ' ', '-', 'é', 'ß', 'ж', '€', '中', '\u{1F600}', '\u{10FFFF}',
];

/// A text of generated characters, written as it is in a string literal.
fn text(g: &mut Gen) -> String {
    (0..g.below(12)).map(|_| *g.pick(CHARACTERS)).collect()
}

fn a_text_counts_the_bytes_of_its_utf8_and_is_read_by_a_byte_at_each_place() {
    generate::check(300, 60, text, |s| {
        let source = format!(
            "fn main() ! console, alloc {{\n  s: str = \"{s}\"\n  print(\"{{s.size}}\")\n  loop i in 0..s.size {{ print(\"{{s[i]}}\") }}\n}}\n"
        );
        let mut lines = vec![s.len().to_string()];
        lines.extend(s.bytes().map(|b| b.to_string()));
        prints(&source, lines);
    });
}

fn count_spaces_of_a_text_is_the_number_of_its_spaces() {
    generate::check(301, 60, text, |s| {
        let source = format!(
            "fn count_spaces(s: str) -> u64 {{\n  var n: u64 = 0\n  loop i in 0..s.size {{\n    if s[i] == ' ' {{ n = wrapping_add(n, 1) }}\n  }}\n  n\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{count_spaces(\"{s}\")}}\")\n}}\n"
        );
        let spaces = s.bytes().filter(|b| *b == b' ').count();
        prints(&source, vec![spaces.to_string()]);
    });
}

/// Bytes, and where the first byte that is not valid is, if one is not.
#[derive(Debug)]
struct Bytes {
    bytes: Vec<u8>,
    broken: Option<usize>,
}

/// Valid bytes of generated characters, or the same with one place broken in
/// a way the standard forbids: a byte no text holds, a continuation byte that
/// starts a character, an overlong form, a surrogate, a character above
/// U+10FFFF, or a character cut off by the end of the bytes or by another
/// character.
fn bytes(g: &mut Gen) -> Bytes {
    let mut bytes = Vec::new();
    let count = g.below(7);
    let broken_at = if g.one_in(2) { Some(g.below(count + 1)) } else { None };
    let kind = g.below(9);
    let mut broken = None;
    for k in 0..=count {
        if broken_at == Some(k) {
            let defect: &[u8] = match kind {
                0 => &[0xff],
                1 => &[0x80],
                2 => &[0xc0, 0x80],
                3 => &[0xe0, 0x80, 0x80],
                4 => &[0xf0, 0x80, 0x80, 0x80],
                5 => &[0xed, 0xa0, 0x80],
                6 => &[0xf4, 0x90, 0x80, 0x80],
                7 => &[0xe2, 0x82],
                _ => &[0xf5, 0x80, 0x80, 0x80],
            };
            broken = Some(bytes.len());
            bytes.extend_from_slice(defect);
            // What follows may be anything that is valid, so that a cut-off
            // character is cut off by the character after it.
        }
        if k < count {
            let mut buffer = [0; 4];
            bytes.extend_from_slice(g.pick(CHARACTERS).encode_utf8(&mut buffer).as_bytes());
        }
    }
    Bytes { bytes, broken }
}

fn utf8_gives_the_text_of_valid_bytes_and_the_first_place_that_is_not() {
    generate::check(302, 150, bytes, |case| {
        let items: Vec<String> = case.bytes.iter().map(u8::to_string).collect();
        let source = format!(
            "fn main() ! console, alloc {{\n  b: List[u8] = [{}]\n  match utf8(b) {{\n    .ok(t) {{\n      print(\"ok {{t.size}}\")\n      loop i in 0..t.size {{ print(\"{{t[i]}}\") }}\n    }}\n    .err(e) {{ print(\"err {{e.at}}\") }}\n  }}\n}}\n",
            items.join(", ")
        );
        let lines = match case.broken {
            None => {
                let mut lines = vec![format!("ok {}", case.bytes.len())];
                lines.extend(case.bytes.iter().map(u8::to_string));
                lines
            }
            Some(at) => vec![format!("err {at}")],
        };
        prints(&source, lines);
    });
}

fn the_examples_of_the_page_on_text_hold() {
    let source = "fn count_spaces(s: str) -> u64 {\n  var n: u64 = 0\n  loop i in 0..s.size {\n    if s[i] == ' ' { n = wrapping_add(n, 1) }\n  }\n  n\n}\n\nfn show(r: Result[str, Utf8Error]) ! console, alloc {\n  match r {\n    .ok(t) { print(\"ok {t}\") }\n    .err(e) { print(\"err {e.at}\") }\n  }\n}\n\nfn main() ! console, alloc {\n  print(\"{count_spaces(\"a b c\")}\")\n  print(\"{\"é\".size}\")\n  show(utf8(\"hi\"))\n  b: List[u8] = [255, 104]\n  show(utf8(b))\n}\n";
    prints(
        source,
        ["2", "2", "ok hi", "err 0"].map(String::from).to_vec(),
    );
}

fn an_index_of_a_text_compiles_exactly_when_an_index_of_a_list_does_with_the_same_error() {
    let shapes = [
        "  loop i in 0..xs.size { print(\"{xs[i]}\") }\n",
        "  k: u64 = 1\n  if k < xs.size { print(\"{xs[k]}\") }\n",
        "  k: u64 = 1\n  print(\"{xs[k]}\")\n",
        "  loop i in 0..xs.size { print(\"{xs[i + 1]}\") }\n",
        "  print(\"{xs[0]}\")\n",
    ];
    let mut compiled = 0;
    for shape in shapes {
        let of = |ty: &str, arg: &str| {
            format!(
                "fn show(xs: {ty}) ! console, alloc {{\n{shape}}}\n\nfn main() ! console, alloc {{ show({arg}) }}\n"
            )
        };
        let list = refused(&of("List[u8]", "[1, 2]"));
        let text = refused(&of("str", "\"ab\""));
        assert_eq!(list, text, "for {shape}");
        compiled += usize::from(text.is_empty());
    }
    assert!(compiled > 0 && compiled < shapes.len(), "{compiled} compile");
}

/// The comparisons, loads, and calls of the functions but `main`, which is what
/// the cost of reading depends on.
fn costs(source: &str) -> (usize, usize, usize) {
    let program = lower(source.as_bytes()).expect("compiles");
    let mut counts = (0, 0, 0);
    for f in program
        .functions
        .iter()
        .enumerate()
        .filter(|(at, f)| *at != program.main && !handler::takes_room(&program, f))
        .map(|(_, f)| f)
    {
        for block in &f.blocks {
            for instruction in &block.instructions {
                match &instruction.op {
                    Op::Compare { .. } => counts.0 += 1,
                    Op::Load { .. } => counts.1 += 1,
                    op if op.is_call() => counts.2 += 1,
                    _ => {}
                }
            }
        }
    }
    counts
}

fn the_size_and_an_index_of_a_text_cost_what_they_cost_on_a_list() {
    let of = |ty: &str, arg: &str| {
        format!(
            "fn total(xs: {ty}) -> u8 {{\n  var sum: u8 = 0\n  loop i in 0..xs.size {{ sum = wrapping_add(sum, xs[i]) }}\n  sum\n}}\n\nfn main() ! alloc {{\n  total({arg})\n}}\n"
        )
    };
    let list = costs(&of("List[u8]", "[1, 2, 3]"));
    let text = costs(&of("str", "\"abc\""));
    assert_eq!(list, text, "list then text");
    assert_ne!(text, (0, 0, 0), "nothing was counted");
}

fn a_text_given_where_bytes_are_expected_is_neither_called_nor_copied() {
    generate::check(305, 40, text, |s| {
        let source = format!(
            "fn total(b: List[u8]) -> Nat {{ b.size }}\n\nfn main() ! console {{\n  if total(\"{s}\") == {} {{ print(\"same\") }}\n}}\n",
            s.len()
        );
        let program = lower(source.as_bytes()).expect("compiles");
        assert!(!program.allocates(), "memory is asked for, for {source}");
        prints(&source, vec!["same".to_string()]);
    });
}

fn utf8_copies_nothing() {
    let take = handler::take();
    let allocations = |source: &str| -> usize {
        let program = lower(source.as_bytes()).expect("compiles");
        program
            .functions
            .iter()
            .flat_map(|f| &f.blocks)
            .flat_map(|b| &b.instructions)
            .filter(|i| {
                matches!(&i.op, Op::Call { function, .. } if program.functions[*function] == take)
            })
            .count()
    };
    let without = "fn main() ! console, alloc {\n  b: List[u8] = [104, 105]\n  print(\"ok\")\n}\n";
    let with = "fn main() ! console, alloc {\n  b: List[u8] = [104, 105]\n  match utf8(b) {\n    .ok(t) { print(\"ok\") }\n    .err(e) { print(\"err\") }\n  }\n}\n";
    assert_eq!(allocations(with), allocations(without));
}
