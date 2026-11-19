//! A list is owned: it has no `Copy`, `push` takes the list it grows and
//! writes in place while the memory has room, and a list is duplicated only
//! where `copy` is written.
//!
//! The generator holds lists in a `var`, in the field of a struct, in a tuple,
//! in an enum, deep inside several of them, and in a list of lists, grows them
//! through functions that take and return them, and reads every list after
//! every push. A model of plain vectors says what is read, how many times the
//! program asks for memory, and how many bytes it moves to grow.

use smith::scalar::runtime::{self, Runtime};
use smith::scalar::{self, Bits, Op, System, Type};
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::growth;
use crate::programs::{lines_added, pos_after, shifted, with_imports};

pub const ONCE: &[Test] = &[
    (
        "a_function_that_gives_a_list_away_and_uses_it_again_is_one_error_whose_fix_is_copy_and_both_with_copy_gives_two_lists",
        a_function_that_gives_a_list_away_and_uses_it_again_is_one_error_whose_fix_is_copy_and_both_with_copy_gives_two_lists,
    ),
    (
        "every_list_a_program_holds_is_read_as_the_model_says_after_every_push",
        every_list_a_program_holds_is_read_as_the_model_says_after_every_push,
    ),
    (
        "a_program_that_grows_lists_at_any_depth_asks_for_memory_and_moves_bytes_for_growth_alone",
        a_program_that_grows_lists_at_any_depth_asks_for_memory_and_moves_bytes_for_growth_alone,
    ),
    (
        "reading_a_list_asks_for_no_memory_and_moves_no_bytes",
        reading_a_list_asks_for_no_memory_and_moves_no_bytes,
    ),
    (
        "a_push_that_has_room_is_12_instructions_or_fewer_in_the_scalar_form",
        a_push_that_has_room_is_12_instructions_or_fewer_in_the_scalar_form,
    ),
    (
        "a_list_has_no_copy_and_neither_has_what_holds_one_and_a_text_has_it",
        a_list_has_no_copy_and_neither_has_what_holds_one_and_a_text_has_it,
    ),
    (
        "copy_gives_a_new_list_of_the_same_elements_and_asks_for_alloc",
        copy_gives_a_new_list_of_the_same_elements_and_asks_for_alloc,
    ),
];

/// The type of an element, and how many bytes it takes in a list.
const ELEMS: &[(&str, usize)] = &[("Byte", 1), ("u32", 4), ("u64", 8)];

/// How many bytes a list takes in a list: its address, size, and room.
const LIST: usize = 24;

fn ceil_log2(n: usize) -> u32 {
    n.next_power_of_two().trailing_zeros()
}

/// Where a list is held, which is what the generator varies.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Kind {
    /// A `var` of the list.
    Plain,
    /// The field of a struct in a `var`, pushed to where it is or through a
    /// function.
    Field,
    /// The first part of a tuple.
    Tuple,
    /// The payload of a case of an enum.
    Enum,
    /// The first part of a tuple in the field of a struct.
    Deep,
    /// A list of lists, which a list built by two pushes is pushed to.
    Lists,
}

const KINDS: [Kind; 6] = [
    Kind::Plain,
    Kind::Field,
    Kind::Tuple,
    Kind::Enum,
    Kind::Deep,
    Kind::Lists,
];

#[derive(Clone, Debug)]
enum Step {
    /// Pushes the value to the list held at the place, through the function
    /// that takes it when `through` says so and where it lies when not.
    Push {
        at: usize,
        value: u64,
        through: bool,
    },
    /// Pushes a list built from the two values to the list of lists.
    Nest { at: usize, values: [u64; 2] },
}

#[derive(Debug)]
struct Program {
    elem: usize,
    holders: Vec<Kind>,
    steps: Vec<Step>,
}

fn program(g: &mut Gen) -> Program {
    let elem = g.below(ELEMS.len());
    let holders: Vec<Kind> = (0..1 + g.below(4)).map(|_| *g.pick(&KINDS)).collect();
    let steps = (0..1 + g.below(14))
        .map(|_| {
            let at = g.below(holders.len());
            let value = g.below(200) as u64;
            match holders[at] {
                Kind::Lists => Step::Nest {
                    at,
                    values: [value, g.below(200) as u64],
                },
                _ => Step::Push {
                    at,
                    value,
                    through: g.one_in(2),
                },
            }
        })
        .collect();
    Program {
        elem,
        holders,
        steps,
    }
}

/// What each holder holds after the steps so far: the list, or the lists.
#[derive(Clone, Debug)]
enum Held {
    One(Vec<u64>),
    Many(Vec<Vec<u64>>),
}

fn held(p: &Program, steps: usize) -> Vec<Held> {
    let mut all: Vec<Held> = p
        .holders
        .iter()
        .map(|k| match k {
            Kind::Lists => Held::Many(Vec::new()),
            _ => Held::One(Vec::new()),
        })
        .collect();
    for step in &p.steps[..steps] {
        match (step, &mut all[match step {
            Step::Push { at, .. } | Step::Nest { at, .. } => *at,
        }]) {
            (Step::Push { value, .. }, Held::One(items)) => items.push(*value),
            (Step::Nest { values, .. }, Held::Many(lists)) => lists.push(values.to_vec()),
            _ => unreachable!("a step fits the holder it is for"),
        }
    }
    all
}

/// What `dump` prints of a list.
fn lines_of(items: &[u64], out: &mut Vec<String>) {
    out.push(items.len().to_string());
    out.extend(items.iter().map(u64::to_string));
}

/// What the program prints when it reads every holder after every step.
fn printed(p: &Program) -> Vec<String> {
    let mut out = Vec::new();
    for steps in 1..=p.steps.len() {
        for h in held(p, steps) {
            match h {
                Held::One(items) => lines_of(&items, &mut out),
                Held::Many(lists) => {
                    out.push(lists.len().to_string());
                    for items in &lists {
                        lines_of(items, &mut out);
                    }
                }
            }
        }
    }
    out
}

const ITEMS: &str = "struct W {
  xs: List[{e}]
}

enum E {
  empty
  full: List[{e}]
}

struct Deep {
  inner: (W, Byte)
}

fn dump(xs: List[{e}]) ! console, alloc {
  print(\"{xs.size}\")
  loop v in xs { print(\"{v}\") }
}

fn read_t(t: (List[{e}], Byte)) ! console, alloc {
  match t {
    (xs, k) { dump(xs) }
  }
}

fn read_e(e: E) ! console, alloc {
  match e {
    .empty { print(\"0\") }
    .full(xs) { dump(xs) }
  }
}

fn read_d(d: Deep) ! console, alloc {
  match d.inner {
    (w, k) { dump(w.xs) }
  }
}

fn read_l(ls: List[List[{e}]]) ! console, alloc {
  print(\"{ls.size}\")
  loop i in 0..ls.size { dump(ls[i]) }
}

fn wpush(w: take W, v: {e}) -> W ! alloc { W(xs: push(w.xs, v)) }

fn tpush(t: take (List[{e}], Byte), v: {e}) -> (List[{e}], Byte) ! alloc {
  match t {
    (xs, k) { (push(xs, v), k) }
  }
}

fn epush(e: take E, v: {e}) -> E ! alloc {
  match e {
    .empty { E.full(push([], v)) }
    .full(xs) { E.full(push(xs, v)) }
  }
}

fn dpush(d: take Deep, v: {e}) -> Deep ! alloc {
  (w, k) = d.inner
  Deep(inner: (W(xs: push(w.xs, v)), k))
}

fn fresh(a: {e}, b: {e}) -> List[{e}] ! alloc {
  var xs: List[{e}] = []
  xs = push(xs, a)
  push(xs, b)
}

";

fn declare(k: Kind, h: usize) -> String {
    match k {
        Kind::Plain => format!("  var h{h}: List[{{e}}] = []\n"),
        Kind::Field => format!("  var h{h}: W = W(xs: [])\n"),
        Kind::Tuple => format!("  var h{h}: (List[{{e}}], Byte) = ([], 7)\n"),
        Kind::Enum => format!("  var h{h}: E = E.empty\n"),
        Kind::Deep => format!("  var h{h}: Deep = Deep(inner: (W(xs: []), 1))\n"),
        Kind::Lists => format!("  var h{h}: List[List[{{e}}]] = []\n"),
    }
}

fn push_text(k: Kind, h: usize, step: &Step) -> String {
    match (k, step) {
        (Kind::Plain, Step::Push { value, .. }) => format!("  h{h} = push(h{h}, {value})\n"),
        (Kind::Field, Step::Push { value, through, .. }) => match through {
            true => format!("  h{h} = wpush(h{h}, {value})\n"),
            false => format!("  h{h}.xs = push(h{h}.xs, {value})\n"),
        },
        (Kind::Tuple, Step::Push { value, .. }) => format!("  h{h} = tpush(h{h}, {value})\n"),
        (Kind::Enum, Step::Push { value, .. }) => format!("  h{h} = epush(h{h}, {value})\n"),
        (Kind::Deep, Step::Push { value, .. }) => format!("  h{h} = dpush(h{h}, {value})\n"),
        (Kind::Lists, Step::Nest { values, .. }) => {
            format!("  h{h} = push(h{h}, fresh({}, {}))\n", values[0], values[1])
        }
        _ => unreachable!("a step fits the holder it is for"),
    }
}

fn read_text(k: Kind, h: usize) -> String {
    match k {
        Kind::Plain => format!("  dump(h{h})\n"),
        Kind::Field => format!("  dump(h{h}.xs)\n"),
        Kind::Tuple => format!("  read_t(h{h})\n"),
        Kind::Enum => format!("  read_e(h{h})\n"),
        Kind::Deep => format!("  read_d(h{h})\n"),
        Kind::Lists => format!("  read_l(h{h})\n"),
    }
}

/// The program as source: every holder is read after every step when `reads`
/// is true, and when it is not the program only grows the lists, so that
/// every request for memory and every byte moved is a list's.
fn source(p: &Program, reads: bool) -> String {
    let mut body = String::new();
    for (h, k) in p.holders.iter().enumerate() {
        body += &declare(*k, h);
    }
    for step in &p.steps {
        let at = match step {
            Step::Push { at, .. } | Step::Nest { at, .. } => *at,
        };
        body += &push_text(p.holders[at], at, step);
        if reads {
            for (h, k) in p.holders.iter().enumerate() {
                body += &read_text(*k, h);
            }
        }
    }
    let effects = if reads { "console, alloc" } else { "alloc" };
    format!("{ITEMS}fn main() ! {effects} {{\n{body}}}\n").replace("{e}", ELEMS[p.elem].0)
}

fn lowered(source: &str) -> scalar::Program {
    let source = &with_imports(source);
    lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"))
}

/// For every program the generator builds, reading every list after every
/// push prints what the model says, so that a list written in place is never
/// seen by another name.
fn every_list_a_program_holds_is_read_as_the_model_says_after_every_push() {
    generate::check(3381, 40, program, |p| {
        let source = source(p, true);
        let stdout: String = printed(p).iter().map(|l| format!("{l}\n")).collect();
        let program = lowered(&source);
        assert_eq!(
            scalar::run(&program),
            scalar::Outcome {
                stdout: stdout.into_bytes(),
                status: 0
            },
            "for {source}"
        );
    });
}

/// How many requests for memory a list of `n` elements takes to grow from
/// empty by pushes, and how many bytes the growth moves, when an element takes
/// `width` bytes.
fn growth(n: usize, width: usize) -> (usize, usize) {
    match n {
        0 => (0, 0),
        _ => (1 + ceil_log2(n) as usize, width * ((1 << ceil_log2(n)) - 1)),
    }
}

/// For every program the generator builds, growing the lists it holds, at any
/// depth, asks for memory once for each doubling and moves the bytes of the
/// list at each doubling that does not grow in place, and nothing else: a push that has room asks for
/// nothing and moves nothing, and the memory of a list is its elements and no
/// word before them.
fn a_program_that_grows_lists_at_any_depth_asks_for_memory_and_moves_bytes_for_growth_alone() {
    generate::check(3391, 60, program, |p| {
        let source = source(p, false);
        let program = lowered(&source);
        let width = ELEMS[p.elem].1;
        let (mut requests, mut bytes) = (0, 0);
        let mut most = 0;
        for h in held(p, p.steps.len()) {
            let (asked, moved) = match h {
                Held::One(items) => {
                    most += 2 * items.len() * width;
                    growth(items.len(), width)
                }
                Held::Many(lists) => {
                    let (mut asked, mut moved) = growth(lists.len(), LIST);
                    most += 2 * lists.len() * LIST;
                    for items in &lists {
                        let (a, m) = growth(items.len(), width);
                        asked += a;
                        moved += m;
                        most += 2 * items.len() * width;
                    }
                    (asked, moved)
                }
            };
            requests += asked;
            bytes += moved;
        }
        let (_, taken) = scalar::run_taking(&program, usize::MAX);
        let (in_place, taken) = (growth::copied_in_place(&taken), growth::rooms(&taken));
        assert_eq!(taken.len(), requests, "requests {taken:?} for {source}");
        for asked in &taken {
            let of = |w: usize| asked % w == 0 && (asked / w).is_power_of_two();
            assert!(of(width) || of(LIST), "{asked} bytes is not a power of two of elements for {source}");
        }
        let (outcome, _, copied) = scalar::run_copying(&program, usize::MAX);
        assert_eq!(outcome.status, 0, "for {source}");
        assert_eq!(copied.iter().sum::<usize>(), bytes - in_place, "copies {copied:?} for {source}");
        assert!(bytes <= most, "{bytes} bytes moved, and at most {most} may be, for {source}");
    });
}

/// Reading a list lends it: its size, an element, a loop over it, and passing
/// it to a parameter that does not take it ask for no memory and move no bytes.
fn reading_a_list_asks_for_no_memory_and_moves_no_bytes() {
    for (t, _) in ELEMS {
        for n in [0usize, 1, 3, 4, 9] {
            let program = |reads: &str| {
                format!(
                    "fn total(xs: List[{t}]) -> {t} {{\n  var sum: {t} = 0\n  loop v in xs {{ sum = wrapping_add(sum, v) }}\n  sum\n}}\n\nfn first(xs: List[{t}]) -> {t} {{ if 0 < xs.size {{ xs[0] }} else {{ 0 }} }}\n\nfn main() ! console, alloc {{\n  var xs: List[{t}] = []\n  loop i in 0..{n} {{ xs = push(xs, 1) }}\n{reads}}}\n"
                )
            };
            let reads = "  a = xs.size\n  b = first(xs)\n  c = total(xs)\n  loop i in 0..xs.size { d = xs[i] }\n  if 0 < a { print(\"read\") }\n  if 0 < wrapping_add(b, c) { print(\"read\") }\n";
            let (plain, read) = (lowered(&program("")), lowered(&program(reads)));
            let measured = |p: &scalar::Program| {
                let (_, taken) = scalar::run_taking(p, usize::MAX);
                let (_, _, copied) = scalar::run_copying(p, usize::MAX);
                (taken, copied)
            };
            assert_eq!(measured(&read), measured(&plain), "{n} elements of {t}");
        }
    }
}

/// The instructions a push that has room runs in the function of the
/// runtime: those of its first block and of the block it goes to when there
/// is room, which is the one that does not ask for memory, itself or by a call.
fn instructions_of_a_push_with_room(scalars: Vec<Type>) -> usize {
    let push = runtime::build(Runtime::Push(scalars), &mut |_| 0);
    let entry = &push.blocks[0];
    let jumps = match &entry.end {
        scalar::End::Switch { jumps, .. } => jumps,
        other => panic!("a push that begins with {other:?}"),
    };
    let asks = |block: usize| {
        push.blocks[block]
            .instructions
            .iter()
            .any(|i| matches!(i.op, Op::System { call: System::Allocate, .. } | Op::Call { .. }))
    };
    let kept: Vec<usize> = jumps.iter().map(|j| j.block).filter(|b| !asks(*b)).collect();
    assert_eq!(kept.len(), 1, "one way through a push does not ask for memory");
    entry.instructions.len() + push.blocks[kept[0]].instructions.len()
}

/// A push that has room, of an element of one scalar, takes 12 instructions or
/// fewer in the scalar form: it compares the size with the room, writes the
/// element after the last, and gives the list with one more.
fn a_push_that_has_room_is_12_instructions_or_fewer_in_the_scalar_form() {
    for bits in [Bits::B8, Bits::B16, Bits::B32, Bits::B64] {
        let found = instructions_of_a_push_with_room(vec![Type::Int(bits)]);
        assert!(found <= 12, "a push of {bits:?} takes {found} instructions");
    }
}

/// `both` of the page holds, and the same function without `copy` is one error
/// at the second use of `xs`, whose fix is to write `copy(xs)`.
fn a_function_that_gives_a_list_away_and_uses_it_again_is_one_error_whose_fix_is_copy_and_both_with_copy_gives_two_lists()
 {
    let both = |first: &str| {
        format!(
            "use std.list (copy)\n\nfn both(xs: take List[Byte]) -> (List[Byte], List[Byte]) ! alloc {{\n  ys = push({first}, 1)\n  (xs, ys)\n}}\n\nfn main() ! console, alloc {{\n  zs: List[Byte] = [7, 8]\n  (a, b) = both(zs)\n  print(\"{{a.size}} {{b.size}}\")\n}}\n"
        )
    };
    let kept = both("copy(xs)");
    let program = lowered(&kept);
    assert_eq!(scalar::run(&program).stdout, b"2 3\n".to_vec());
    let refused = both("xs");
    let full = with_imports(&refused);
    let errors = check(full.as_bytes()).expect_err("a list used after it was given away");
    assert_eq!(errors.len(), 1, "{errors:?}");
    let second = pos_after(&refused[..refused.find("(xs, ys)").unwrap() + 1]);
    let second = shifted(second, lines_added(&refused, &full));
    assert_eq!(errors[0].pos, second);
    assert!(
        errors[0].fix.as_deref().is_some_and(|f| f.contains("copy(xs)")),
        "{errors:?}"
    );
}

/// A list has no `Copy`, and neither has a struct, a tuple, an enum, or a list
/// that holds one; a text has it, as has what holds only numbers and texts.
fn a_list_has_no_copy_and_neither_has_what_holds_one_and_a_text_has_it() {
    // Each type, whether it has `Copy`, and a value of it.
    let types: &[(&str, bool, &str)] = &[
        ("List[Byte]", false, "[]"),
        ("List[str]", false, "[]"),
        ("List[List[Byte]]", false, "[]"),
        ("W", false, "W(xs: [])"),
        ("(List[Byte], Byte)", false, "([], 1)"),
        ("Option[List[Byte]]", false, "Option.none"),
        ("E", false, "E.none"),
        ("str", true, "\"a\""),
        ("Byte", true, "1"),
        ("(str, Byte)", true, "(\"a\", 1)"),
        ("Option[str]", true, "Option.some(\"a\")"),
        ("V", true, "V(s: \"a\", n: 1)"),
    ];
    let items = "struct W {\n  xs: List[Byte]\n}\n\nenum E {\n  none\n  some: List[Byte]\n}\n\nstruct V {\n  s: str\n  n: Byte\n}\n\n";
    for (ty, copies, value) in types {
        // A call that asks for `Copy` of the type, and a value used twice.
        let asks = format!(
            "{items}fn twin[T: Copy](x: T) -> u64 {{ 1 }}\n\nfn use_it(x: {ty}) -> u64 {{ twin(x) }}\n\nfn main() ! alloc {{\n  x: {ty} = {value}\n  n = use_it(x)\n}}\n"
        );
        let errors = check(with_imports(&asks).as_bytes()).err().unwrap_or_default();
        assert_eq!(errors.is_empty(), *copies, "{errors:?} for {ty}");
        if !copies {
            assert!(
                errors.iter().any(|e| e.message.contains("`Copy`")),
                "{errors:?} does not name `Copy` for {ty}"
            );
        }
        let again = format!(
            "{items}fn twice(x: take {ty}) -> ({ty}, {ty}) {{ (x, x) }}\n\nfn main() ! alloc {{\n  x: {ty} = {value}\n  r = twice(x)\n}}\n"
        );
        let errors = check(with_imports(&again).as_bytes()).err().unwrap_or_default();
        assert_eq!(errors.is_empty(), *copies, "{errors:?} for {ty}");
    }
}

/// `copy(xs)` gives a new list of the same elements that no push to either
/// changes in the other, and a function that calls it asks for `alloc`.
fn copy_gives_a_new_list_of_the_same_elements_and_asks_for_alloc() {
    generate::check(3401, 40, |g: &mut Gen| {
        let n = g.below(7);
        let items: Vec<u64> = (0..n).map(|_| g.below(200) as u64).collect();
        (g.below(ELEMS.len()), items, g.below(200) as u64, g.below(200) as u64)
    }, |(elem, items, to_copy, to_original)| {
        let t = ELEMS[*elem].0;
        let written: Vec<String> = items.iter().map(u64::to_string).collect();
        let source = format!(
            "use std.list (copy)\n\nfn dump(xs: List[{t}]) ! console, alloc {{\n  print(\"{{xs.size}}\")\n  loop v in xs {{ print(\"{{v}}\") }}\n}}\n\nfn main() ! console, alloc {{\n  var xs: List[{t}] = [{}]\n  var ys = copy(xs)\n  dump(ys)\n  ys = push(ys, {to_copy})\n  xs = push(xs, {to_original})\n  dump(xs)\n  dump(ys)\n}}\n",
            written.join(", ")
        );
        let mut lines = Vec::new();
        lines_of(items, &mut lines);
        let mut grown = items.clone();
        grown.push(*to_original);
        lines_of(&grown, &mut lines);
        let mut other = items.clone();
        other.push(*to_copy);
        lines_of(&other, &mut lines);
        let expected: String = lines.iter().map(|l| format!("{l}\n")).collect();
        assert_eq!(scalar::run(&lowered(&source)).stdout, expected.into_bytes(), "for {source}");
    });
    let quiet = "use std.list (copy)\n\nfn twice(xs: List[Byte]) -> List[Byte] { copy(xs) }\n\nfn main() {\n  xs: List[Byte] = []\n  ys = twice(xs)\n}\n";
    let errors = check(with_imports(quiet).as_bytes()).err().unwrap_or_default();
    assert!(
        errors.iter().any(|e| e.message.contains("`alloc`")),
        "{errors:?} does not name `alloc`"
    );
}
