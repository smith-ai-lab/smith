//! A `Slice` is a run of elements that cannot change: `freeze` makes one from
//! a list with nothing copied, a piece of it shares its memory, it has `Copy`,
//! and a function that reads takes a lent list and is given either.
//!
//! The generators freeze a list, build other lists that ask for memory and
//! write in it, and then read every element of the slice and of every piece of
//! it, and of the pieces of the pieces: a slice read after memory was reused
//! is a wrong answer. A model of plain vectors says what is read.

use smith::scalar;
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::programs::pos_after;

pub const ONCE: &[Test] = &[
    (
        "the_slice_examples_of_the_page_give_what_the_page_says",
        the_slice_examples_of_the_page_give_what_the_page_says,
    ),
    (
        "every_element_of_a_frozen_slice_and_of_every_piece_is_read_after_other_lists_reuse_memory",
        every_element_of_a_frozen_slice_and_of_every_piece_is_read_after_other_lists_reuse_memory,
    ),
    (
        "a_piece_compiles_exactly_when_both_ends_are_proven_inside_and_has_the_elements_between",
        a_piece_compiles_exactly_when_both_ends_are_proven_inside_and_has_the_elements_between,
    ),
    (
        "freeze_asks_for_no_memory_moves_no_byte_and_runs_no_instruction_more",
        freeze_asks_for_no_memory_moves_no_byte_and_runs_no_instruction_more,
    ),
    (
        "a_list_given_to_freeze_is_given_away_and_a_use_of_it_afterwards_is_one_error",
        a_list_given_to_freeze_is_given_away_and_a_use_of_it_afterwards_is_one_error,
    ),
    (
        "a_slice_has_copy_and_is_bound_stored_and_returned_freely",
        a_slice_has_copy_and_is_bound_stored_and_returned_freely,
    ),
    (
        "a_slice_a_list_and_a_text_are_passed_where_a_list_is_lent_and_a_slice_is_refused_where_one_is_taken",
        a_slice_a_list_and_a_text_are_passed_where_a_list_is_lent_and_a_slice_is_refused_where_one_is_taken,
    ),
    (
        "copy_of_a_slice_gives_a_new_list_and_asks_for_alloc",
        copy_of_a_slice_gives_a_new_list_and_asks_for_alloc,
    ),
    (
        "a_slice_written_out_is_data_in_the_executable_and_asks_for_no_alloc_and_no_memory",
        a_slice_written_out_is_data_in_the_executable_and_asks_for_no_alloc_and_no_memory,
    ),
    (
        "nothing_is_pushed_to_and_no_element_is_assigned_in_a_slice",
        nothing_is_pushed_to_and_no_element_is_assigned_in_a_slice,
    ),
];

/// The type of an element, and how many bytes it takes.
const ELEMS: &[(&str, usize)] = &[("u8", 1), ("u32", 4), ("u64", 8)];

fn lowered(source: &str) -> scalar::Program {
    lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"))
}

/// What `dump` prints of a run of elements.
fn lines_of(items: &[u64], out: &mut Vec<String>) {
    out.push(items.len().to_string());
    out.extend(items.iter().map(u64::to_string));
}

fn expected(lines: &[String]) -> Vec<u8> {
    lines.iter().map(|l| format!("{l}\n")).collect::<String>().into_bytes()
}

/// `evens`, `halves`, `total`, and `demo` of the page give what the page says.
/// `halves` takes where to cut, as division is not built yet, and `demo` has no
/// piece of a list, which is the next issue; `out` of the page's `evens` is a
/// word of the language, so the list is called `built`.
fn the_slice_examples_of_the_page_give_what_the_page_says() {
    let source = "fn evens(n: u64) -> Slice[u64] ! alloc {
  var built: List[u64] = []
  loop i in 0..n { built = push(built, wrapping_mul(i, 2)) }
  freeze(built)
}

struct Halves {
  left: Slice[u64]
  right: Slice[u64]
}

fn halves(s: Slice[u64], mid: Nat) -> Halves
  requires mid <= s.size
{
  Halves(left: s[..mid], right: s[mid..])
}

fn total(xs: List[u64]) -> u64 {
  var t: u64 = 0
  loop i in 0..xs.size { t = wrapping_add(t, xs[i]) }
  t
}

fn demo(building: List[u64], done: Slice[u64]) -> u64 {
  wrapping_add(total(building), total(done))
}

fn main() ! console, alloc {
  e = evens(3)
  if 3 <= e.size { print(\"{total(e)} {e.size} {e[2]}\") }
  f = evens(5)
  if 3 <= f.size {
    h = halves(f, 3)
    print(\"{h.left.size} {total(h.left)} {h.right.size} {total(h.right)}\")
  }
  building: List[u64] = [10, 20]
  print(\"{demo(building, f)}\")
}
";
    let out = scalar::run(&lowered(source));
    assert_eq!(out.stdout, b"6 3 4\n3 6 2 14\n50\n".to_vec());
}

/// Where a piece begins and ends.
type Span = (usize, usize);

/// What the generator draws for a frozen list.
#[derive(Debug)]
struct Frozen {
    elem: usize,
    items: Vec<u64>,
    /// Pieces of the slice: both ends, and for each a piece of that piece.
    pieces: Vec<(Span, Option<Span>)>,
    /// The sizes of lists built after the freeze, which ask for memory.
    others: Vec<usize>,
}

fn frozen(g: &mut Gen) -> Frozen {
    let elem = g.below(ELEMS.len());
    let n = g.below(20);
    let items = (0..n).map(|_| g.below(100) as u64).collect();
    let pieces = (0..g.below(4))
        .map(|_| {
            let a = g.below(n + 1);
            let b = a + g.below(n - a + 1);
            let inner = g
                .one_in(2)
                .then(|| {
                    let c = g.below(b - a + 1);
                    (c, c + g.below(b - a - c + 1))
                });
            ((a, b), inner)
        })
        .collect();
    let others = (0..1 + g.below(3)).map(|_| g.below(40)).collect();
    Frozen {
        elem,
        items,
        pieces,
        others,
    }
}

/// Every run of elements the program reads, in order: the slice, each piece,
/// the piece of each piece, the short forms, and the lists built after it.
fn read_by(f: &Frozen) -> Vec<Vec<u64>> {
    let n = f.items.len();
    let mut all: Vec<Vec<u64>> = Vec::new();
    for (k, base) in f.others.iter().enumerate() {
        all.push(vec![150 + 10 * k as u64; *base]);
    }
    all.push(f.items.clone());
    for ((a, b), inner) in &f.pieces {
        all.push(f.items[*a..*b].to_vec());
        if let Some((c, d)) = inner {
            all.push(f.items[*a..*b][*c..*d].to_vec());
        }
    }
    all.push(f.items[..n / 2].to_vec());
    all.push(f.items[n / 2..].to_vec());
    all
}

fn frozen_source(f: &Frozen) -> String {
    let t = ELEMS[f.elem].0;
    let n = f.items.len();
    let mut reads = String::from("  dump(s)\n");
    for (k, ((a, b), inner)) in f.pieces.iter().enumerate() {
        reads += &format!("  dump(s[{a}..{b}])\n");
        if let Some((c, d)) = inner {
            reads += &format!("  p{k} = s[{a}..{b}]\n  dump(p{k}[{c}..{d}])\n");
        }
    }
    reads += &format!("  dump(s[..{}])\n  dump(s[{}..])\n", n / 2, n / 2);
    let mut main = format!("  var a: List[{t}] = []\n");
    for v in &f.items {
        main += &format!("  a = push(a, {v})\n");
    }
    main += "  s = freeze(a)\n";
    for (k, size) in f.others.iter().enumerate() {
        main += &format!("  o{k} = build({size}, {})\n", 150 + 10 * k);
    }
    for k in 0..f.others.len() {
        main += &format!("  dump(o{k})\n");
    }
    main += &format!("  if {n} <= s.size {{ read(s) }}\n");
    format!(
        "fn dump(xs: List[{t}]) ! console, alloc {{
  print(\"{{xs.size}}\")
  loop v in xs {{ print(\"{{v}}\") }}
}}

fn build(n: u64, base: {t}) -> List[{t}] ! alloc {{
  var xs: List[{t}] = []
  loop i in 0..n {{ xs = push(xs, base) }}
  xs
}}

fn read(s: Slice[{t}]) ! console, alloc
  requires s.size >= {n}
{{
{reads}}}

fn main() ! console, alloc {{
{main}}}
"
    )
}

/// A list is frozen, then other lists are built, which grow, ask for memory,
/// and write in it, and then every element of the slice and of every piece of
/// it is read. What is read is what was frozen.
fn every_element_of_a_frozen_slice_and_of_every_piece_is_read_after_other_lists_reuse_memory() {
    generate::check(3501, 40, frozen, |f| {
        let source = frozen_source(f);
        let mut lines = Vec::new();
        for run in read_by(f) {
            lines_of(&run, &mut lines);
        }
        // The program reads the lists it built first, then the slice, then
        // its pieces in the order they are written, then the short forms.
        let out = scalar::run(&lowered(&source));
        assert_eq!(out.stdout, expected(&lines), "for {source}");
        assert_eq!(out.status, 0, "for {source}");
    });
}

/// How a piece is written.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Ends {
    Both,
    Start,
    End,
}

/// A piece of a slice of `n` elements, with ends that may lie outside: it
/// compiles exactly when `arith` proves `a <= b` and `b <= size`, and then
/// has the elements between them.
fn a_piece_compiles_exactly_when_both_ends_are_proven_inside_and_has_the_elements_between() {
    generate::check(
        3511,
        60,
        |g: &mut Gen| {
            let ends = *g.pick(&[Ends::Both, Ends::Start, Ends::End]);
            (g.below(ELEMS.len()), ends, g.below(7), g.below(9), g.below(9))
        },
        |&(elem, ends, n, a, b)| {
            let t = ELEMS[elem].0;
            let written = match ends {
                Ends::Both => format!("s[{a}..{b}]"),
                Ends::Start => format!("s[{a}..]"),
                Ends::End => format!("s[..{b}]"),
            };
            let items: Vec<String> = (0..n).map(|k| (k + 1).to_string()).collect();
            let source = format!(
                "fn dump(xs: List[{t}]) ! console, alloc {{
  print(\"{{xs.size}}\")
  loop v in xs {{ print(\"{{v}}\") }}
}}

fn cut(s: Slice[{t}]) ! console, alloc
  requires s.size >= {n}
{{
  dump({written})
}}

fn main() ! console, alloc {{
  s: Slice[{t}] = [{}]
  cut(s)
}}
",
                items.join(", ")
            );
            let (from, to) = match ends {
                Ends::Both => (a, b),
                Ends::Start => (a, n),
                Ends::End => (0, b),
            };
            // The slice is known only to have `n` elements or more.
            let proven = from <= to && to <= n;
            match check(source.as_bytes()) {
                Ok(_) => {
                    assert!(proven, "{written} of {n} elements compiled for {source}");
                    let run: Vec<u64> = (from as u64 + 1..=to as u64).collect();
                    let mut lines = Vec::new();
                    lines_of(&run, &mut lines);
                    let out = scalar::run(&lowered(&source));
                    assert_eq!(out.stdout, expected(&lines), "for {source}");
                }
                Err(errors) => {
                    assert!(!proven, "{written} of {n} elements was refused: {errors:?} for {source}");
                    assert_eq!(errors.len(), 1, "{errors:?} for {source}");
                    assert!(errors[0].message.contains("cannot prove"), "{errors:?}");
                }
            }
        },
    );
}

/// `freeze` is the address and the size and no instruction more: the program
/// that freezes a list before it reads it asks for the same memory, moves the
/// same bytes, and runs no more instructions than the one that reads the list.
fn freeze_asks_for_no_memory_moves_no_byte_and_runs_no_instruction_more() {
    for (t, _) in ELEMS {
        for n in [0usize, 1, 5, 9] {
            let program = |read: &str| {
                format!(
                    "fn total(xs: List[{t}]) -> {t} {{
  var sum: {t} = 0
  loop v in xs {{ sum = wrapping_add(sum, v) }}
  sum
}}

fn main() ! console, alloc {{
  var xs: List[{t}] = []
  loop i in 0..{n} {{ xs = push(xs, 3) }}
{read}}}
"
                )
            };
            let plain = lowered(&program("  print(\"{total(xs)}\")\n"));
            let frozen = lowered(&program("  s = freeze(xs)\n  print(\"{total(s)}\")\n"));
            let measured = |p: &scalar::Program| {
                let (_, taken) = scalar::run_taking(p, usize::MAX);
                let (_, _, copied) = scalar::run_copying(p, usize::MAX);
                let (out, ran) = scalar::run_measured(p);
                (taken, copied, ran, out.stdout)
            };
            let (a, b) = (measured(&plain), measured(&frozen));
            assert_eq!((&a.0, &a.1, &a.3), (&b.0, &b.1, &b.3), "{n} elements of {t}");
            assert!(b.2 <= a.2, "{} instructions with freeze and {} without, for {n} of {t}", b.2, a.2);
        }
    }
}

/// `freeze` gives its list away: a use of the list afterwards is the error
/// ownership gives, at the second use.
fn a_list_given_to_freeze_is_given_away_and_a_use_of_it_afterwards_is_one_error() {
    let program = |second: &str| {
        format!(
            "fn keep(xs: take List[u8]) -> (Slice[u8], Nat) {{\n  s = freeze(xs)\n  (s, {second})\n}}\n\nfn main() ! alloc {{\n  xs: List[u8] = [1, 2]\n  r = keep(xs)\n}}\n"
        )
    };
    assert!(check(program("s.size").as_bytes()).is_ok());
    let refused = program("xs.size");
    let errors = check(refused.as_bytes()).expect_err("a list used after it was frozen");
    assert_eq!(errors.len(), 1, "{errors:?}");
    assert_eq!(errors[0].pos, pos_after(&refused[..refused.find("xs.size").unwrap()]));
    assert!(errors[0].message.contains("given away"), "{errors:?}");
}

/// A slice has `Copy`, so it is bound twice, stored in a struct and a tuple,
/// returned, and read through every name; a list has not.
fn a_slice_has_copy_and_is_bound_stored_and_returned_freely() {
    generate::check(
        3521,
        30,
        |g: &mut Gen| {
            let n = 1 + g.below(6);
            (g.below(ELEMS.len()), (0..n).map(|_| g.below(100) as u64).collect::<Vec<_>>())
        },
        |(elem, items)| {
            let t = ELEMS[*elem].0;
            let written: Vec<String> = items.iter().map(u64::to_string).collect();
            let source = format!(
                "struct Held {{
  all: Slice[{t}]
  tag: u8
}}

fn dump(xs: List[{t}]) ! console, alloc {{
  print(\"{{xs.size}}\")
  loop v in xs {{ print(\"{{v}}\") }}
}}

fn hold(s: Slice[{t}]) -> Held {{ Held(all: s, tag: 1) }}

fn pair(s: Slice[{t}]) -> (Slice[{t}], Slice[{t}]) {{ (s, s) }}

fn main() ! console, alloc {{
  var a: List[{t}] = []
{pushes}  s = freeze(a)
  t = s
  h = hold(s)
  (x, y) = pair(t)
  dump(s)
  dump(t)
  dump(h.all)
  dump(x)
  dump(y)
}}
",
                pushes = written.iter().map(|v| format!("  a = push(a, {v})\n")).collect::<String>()
            );
            let mut lines = Vec::new();
            for _ in 0..5 {
                lines_of(items, &mut lines);
            }
            assert_eq!(scalar::run(&lowered(&source)).stdout, expected(&lines), "for {source}");
        },
    );
    // Each type, whether it has `Copy`, and a value of it.
    let items = "struct S {\n  all: Slice[u8]\n}\n\nstruct W {\n  xs: List[u8]\n}\n\n";
    let types: &[(&str, bool, &str)] = &[
        ("Slice[u8]", true, "[1]"),
        ("Slice[u64]", true, "[]"),
        ("S", true, "S(all: [1])"),
        ("(Slice[u8], u8)", true, "([1], 2)"),
        ("Option[Slice[u8]]", true, "Option.some([1])"),
        ("Slice[Slice[u8]]", true, "[]"),
        ("List[u8]", false, "[1]"),
        ("W", false, "W(xs: [1])"),
    ];
    for (ty, copies, value) in types {
        let source = format!(
            "{items}fn twin[T: Copy](x: T) -> u64 {{ 1 }}\n\nfn use_it(x: {ty}) -> u64 {{ twin(x) }}\n\nfn main() ! alloc {{\n  x: {ty} = {value}\n  n = use_it(x)\n}}\n"
        );
        let errors = check(source.as_bytes()).err().unwrap_or_default();
        let nested = *ty == "Slice[Slice[u8]]";
        // A slice of slices is written out with elements that are not numbers.
        if nested {
            continue;
        }
        assert_eq!(errors.is_empty(), *copies, "{errors:?} for {ty}");
    }
}

/// A slice, a list, and a text of bytes are each passed to a parameter that is
/// a list and not taken, with no memory asked for and no byte copied. A slice
/// is refused where a parameter takes a list, and the fix names `copy`.
fn a_slice_a_list_and_a_text_are_passed_where_a_list_is_lent_and_a_slice_is_refused_where_one_is_taken() {
    let program = |given: &str, total: u8| {
        format!(
            "fn total(xs: List[u8]) -> u8 {{
  var sum: u8 = 0
  loop v in xs {{ sum = wrapping_add(sum, v) }}
  sum
}}

fn main() ! console {{
{given}  if got == {total} {{ print(\"ok\") }} else {{ print(\"bad\") }}
}}
"
        )
    };
    for (given, total) in [
        ("  s: Slice[u8] = [1, 2, 3]\n  got = total(s)\n", 6),
        ("  s: Slice[u8] = [1, 2, 3]\n  got = total(s[1..3])\n", 5),
        ("  got = total(\"ab\")\n", 195),
    ] {
        let source = program(given, total);
        let built = lowered(&source);
        let (out, taken) = scalar::run_taking(&built, usize::MAX);
        let (_, _, copied) = scalar::run_copying(&built, usize::MAX);
        assert_eq!(out.stdout, b"ok\n".to_vec(), "for {source}");
        assert!(taken.is_empty(), "{taken:?} for {source}");
        assert_eq!(copied.iter().sum::<usize>(), 0, "{copied:?} for {source}");
    }
    let taking = "use std.list (copy)\n\nfn grow(xs: take List[u8]) -> List[u8] ! alloc { push(xs, 1) }\n\nfn go(s: Slice[u8]) -> List[u8] ! alloc { grow(SLICE) }\n\nfn main() ! alloc {\n  s: Slice[u8] = [1, 2]\n  r = go(s)\n}\n";
    assert!(check(taking.replace("SLICE", "copy(s)").as_bytes()).is_ok());
    let refused = taking.replace("SLICE", "s");
    let errors = check(refused.as_bytes()).expect_err("a slice where a list is taken");
    assert_eq!(errors.len(), 1, "{errors:?}");
    assert_eq!(errors[0].pos, pos_after(&refused[..refused.find("grow(s)").unwrap() + 5]));
    assert!(errors[0].fix.as_deref().is_some_and(|f| f.contains("copy(s)")), "{errors:?}");
}

/// `copy(s)` gives a new list of the elements of the slice that a push does not
/// change in the slice, and a function that calls it carries `alloc`.
fn copy_of_a_slice_gives_a_new_list_and_asks_for_alloc() {
    generate::check(
        3531,
        30,
        |g: &mut Gen| {
            let n = g.below(8);
            (g.below(ELEMS.len()), (0..n).map(|_| g.below(100) as u64).collect::<Vec<_>>(), g.below(100) as u64)
        },
        |(elem, items, extra)| {
            let t = ELEMS[*elem].0;
            let written: Vec<String> = items.iter().map(u64::to_string).collect();
            let source = format!(
                "use std.list (copy)

fn dump(xs: List[{t}]) ! console, alloc {{
  print(\"{{xs.size}}\")
  loop v in xs {{ print(\"{{v}}\") }}
}}

fn main() ! console, alloc {{
  s: Slice[{t}] = [{}]
  var xs = copy(s)
  xs = push(xs, {extra})
  dump(s)
  dump(xs)
}}
",
                written.join(", ")
            );
            let mut lines = Vec::new();
            lines_of(items, &mut lines);
            let mut grown = items.clone();
            grown.push(*extra);
            lines_of(&grown, &mut lines);
            assert_eq!(scalar::run(&lowered(&source)).stdout, expected(&lines), "for {source}");
        },
    );
    let quiet = "use std.list (copy)\n\nfn twice(s: Slice[u8]) -> List[u8] { copy(s) }\n\nfn main() ! alloc {\n  s: Slice[u8] = [1]\n  r = twice(s)\n}\n";
    let errors = check(quiet.as_bytes()).err().unwrap_or_default();
    assert!(errors.iter().any(|e| e.message.contains("`alloc`")), "{errors:?} does not name `alloc`");
}

/// A slice written out is data in the executable: the function that holds it
/// carries no `alloc`, a run asks for no memory, and each element is read as
/// it was written. A list written the same way carries `alloc`.
fn a_slice_written_out_is_data_in_the_executable_and_asks_for_no_alloc_and_no_memory() {
    generate::check(
        3541,
        40,
        |g: &mut Gen| {
            let n = 1 + g.below(8);
            let elem = g.below(ELEMS.len());
            let max = [200, 100_000, 1 << 40][elem];
            (elem, (0..n).map(|_| g.below(max)).collect::<Vec<_>>(), g.below(n))
        },
        |(elem, items, at)| {
            let t = ELEMS[*elem].0;
            let written: Vec<String> = items.iter().map(usize::to_string).collect();
            let source = format!(
                "fn pick() -> {t} {{
  s: Slice[{t}] = [{}]
  s[{at}]
}}

fn main() ! console {{
  v = pick()
  if v == {} {{ print(\"ok\") }} else {{ print(\"bad\") }}
}}
",
                written.join(", "),
                items[*at]
            );
            let built = lowered(&source);
            let (out, taken) = scalar::run_taking(&built, usize::MAX);
            assert_eq!(out.stdout, b"ok\n".to_vec(), "for {source}");
            assert!(taken.is_empty(), "{taken:?} for {source}");
        },
    );
    let list = "fn pick() -> u8 {\n  s: List[u8] = [2, 3]\n  s[1]\n}\n\nfn main() ! alloc {\n  v = pick()\n}\n";
    let errors = check(list.as_bytes()).err().unwrap_or_default();
    assert!(errors.iter().any(|e| e.message.contains("`alloc`")), "{errors:?} does not name `alloc`");
}

/// A slice cannot change: `push` is refused on one, and so is an assignment to
/// one of its elements.
fn nothing_is_pushed_to_and_no_element_is_assigned_in_a_slice() {
    let pushed = "fn more(s: Slice[u8]) -> Slice[u8] ! alloc { push(s, 1) }\n\nfn main() ! alloc {\n  s: Slice[u8] = [1]\n  r = more(s)\n}\n";
    let errors = check(pushed.as_bytes()).expect_err("a push to a slice");
    assert!(errors.iter().all(|e| e.message.contains("slice")), "{errors:?}");
    let assigned = "fn set(s: Slice[u8]) {\n  s[0] = 1\n}\n\nfn main() {\n  s: Slice[u8] = [1]\n  set(s)\n}\n";
    assert!(check(assigned.as_bytes()).is_err());
}
