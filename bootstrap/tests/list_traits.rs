//! `Eq` of a `List` and of a `Slice`, which are equal when they have the same
//! size and equal elements in order, and what a list holds that lacks `Eq`.
//! The `Fmt` and `Repr` of a list are held by the generator of `reprs.rs`.

use smith::check;
use smith::lower;
use smith::scalar;

use crate::Test;
use crate::generate::{self, Gen};
use crate::numbers::with_header;
use crate::reprs;

pub const ONCE: &[Test] = &[
    (
        "two_lists_or_slices_are_equal_when_they_have_the_same_size_and_equal_elements_in_order",
        two_lists_or_slices_are_equal_when_they_have_the_same_size_and_equal_elements_in_order,
    ),
    (
        "a_list_is_written_as_the_page_says_where_a_struct_a_tuple_an_option_or_a_list_holds_it",
        a_list_is_written_as_the_page_says_where_a_struct_a_tuple_an_option_or_a_list_holds_it,
    ),
    (
        "a_list_of_what_lacks_fmt_or_repr_is_refused_naming_the_element",
        a_list_of_what_lacks_fmt_or_repr_is_refused_naming_the_element,
    ),
    (
        "a_slice_is_written_as_a_list_is_alone_and_in_a_tuple_a_struct_and_an_option",
        a_slice_is_written_as_a_list_is_alone_and_in_a_tuple_a_struct_and_an_option,
    ),
    (
        "a_list_whose_element_has_no_eq_is_refused_naming_the_element_in_every_place",
        a_list_whose_element_has_no_eq_is_refused_naming_the_element_in_every_place,
    ),
];

fn stdout(source: &str) -> String {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
    let out = scalar::run(&program);
    assert_eq!(out.status, 0, "for\n{source}");
    String::from_utf8(out.stdout).unwrap()
}

/// A value of an element type, which two lists are made of.
#[derive(Clone, Debug, PartialEq)]
enum Item {
    Number(u64),
    Text(&'static str),
    Flag(bool),
    Pair(u64, &'static str),
    Record(u64, &'static str),
    List(Vec<Item>),
}

const TEXTS: [&str; 3] = ["a", "", "b c"];

/// The kind of an element, and how a type of it is written.
#[derive(Clone, Copy, Debug)]
enum Kind {
    Number,
    Text,
    Flag,
    Pair,
    Record,
    List(&'static Kind),
}

const LEAVES: [Kind; 5] = [Kind::Number, Kind::Text, Kind::Flag, Kind::Pair, Kind::Record];
const ONE: Kind = Kind::Number;
const TWO: Kind = Kind::List(&ONE);

impl Kind {
    fn written(&self) -> String {
        match self {
            Kind::Number => "Nat".into(),
            Kind::Text => "str".into(),
            Kind::Flag => "bool".into(),
            Kind::Pair => "(Nat, str)".into(),
            Kind::Record => "Record".into(),
            Kind::List(of) => format!("List[{}]", of.written()),
        }
    }

    fn item(&self, g: &mut Gen) -> Item {
        match self {
            Kind::Number => Item::Number(g.below(3) as u64),
            Kind::Text => Item::Text(TEXTS[g.below(3)]),
            Kind::Flag => Item::Flag(g.one_in(2)),
            Kind::Pair => Item::Pair(g.below(2) as u64, TEXTS[g.below(2)]),
            Kind::Record => Item::Record(g.below(2) as u64, TEXTS[g.below(2)]),
            Kind::List(of) => Item::List((0..g.below(3)).map(|_| of.item(g)).collect()),
        }
    }
}

fn expr(item: &Item) -> String {
    let list = |of: &[Item]| -> String {
        let items: Vec<String> = of.iter().map(expr).collect();
        items.join(", ")
    };
    match item {
        Item::Number(n) => n.to_string(),
        Item::Text(t) => format!("\"{t}\""),
        Item::Flag(b) => b.to_string(),
        Item::Pair(n, t) => format!("({n}, \"{t}\")"),
        Item::Record(n, t) => format!("Record(number: {n}, text: \"{t}\")"),
        Item::List(of) => format!("[{}]", list(of)),
    }
}

/// Two lists of elements of one type, which are often the same list.
fn two_lists(g: &mut Gen) -> (Kind, Vec<Item>, Vec<Item>, usize) {
    let kind = match g.below(7) {
        0..=4 => LEAVES[g.below(5)],
        5 => TWO,
        _ => Kind::List(&TWO),
    };
    let xs: Vec<Item> = (0..g.below(4)).map(|_| kind.item(g)).collect();
    let mut ys = xs.clone();
    match g.below(4) {
        0 => ys = (0..g.below(4)).map(|_| kind.item(g)).collect(),
        1 if !ys.is_empty() => {
            let at = g.below(ys.len());
            ys[at] = kind.item(g);
        }
        2 => ys.push(kind.item(g)),
        _ => {}
    }
    (kind, xs, ys, g.below(4))
}

fn two_lists_or_slices_are_equal_when_they_have_the_same_size_and_equal_elements_in_order() {
    generate::check(4020, 40, two_lists, |(kind, xs, ys, place)| {
        let ty = kind.written();
        let (a, b): (Vec<String>, Vec<String>) = (
            xs.iter().map(expr).collect(),
            ys.iter().map(expr).collect(),
        );
        let (a, b) = (format!("[{}]", a.join(", ")), format!("[{}]", b.join(", ")));
        // A list or a slice, alone or held by another type.
        let (collection, held) = match place {
            0 => ("List", "{xs == ys}"),
            1 if matches!(kind, Kind::Number) => ("Slice", "{xs == ys}"),
            1 => ("List", "{xs == ys}"),
            2 => ("List", "{(xs, 1) == (ys, 1)}"),
            _ => ("List", "{Option.some(xs) == Option.some(ys)}"),
        };
        // A tuple or an `Option` that holds a list gives it away.
        let generic = if *place < 2 { " {same(xs, ys)}" } else { "" };
        let program = with_header(format!(
            "struct Record {{\n  number: Nat\n  text: str\n}}\n\nfn same[T: Eq](x: T, y: T) -> bool {{ x == y }}\n\nfn main() ! console, alloc {{\n  xs: {collection}[{ty}] = {a}\n  ys: {collection}[{ty}] = {b}\n  print(\"{held}{generic}\")\n}}\n"
        ));
        let want = match place {
            0 | 1 => format!("{0} {0}\n", xs == ys),
            _ => format!("{}\n", xs == ys),
        };
        assert_eq!(stdout(&program), want, "for\n{program}");
    });
}

fn a_list_whose_element_has_no_eq_is_refused_naming_the_element_in_every_place() {
    let program = |ty: &str, make: &str, ask: &str| {
        with_header(format!(
            "struct Hidden {{\n  key: str\n}}\n\nwithout Eq for Hidden\n\nfn main() ! console, alloc {{\n  xs: {ty} = {make}\n  ys: {ty} = {make}\n  print(\"{{{ask}}}\")\n}}\n"
        ))
    };
    for (ty, make, ask) in [
        ("List[Hidden]", "[]", "xs == ys"),
        ("Slice[Hidden]", "[]", "xs == ys"),
        ("List[List[Hidden]]", "[]", "xs == ys"),
        ("List[(Nat, Hidden)]", "[]", "xs == ys"),
        ("(List[Hidden], Nat)", "([], 1)", "xs == ys"),
        ("Option[List[Hidden]]", ".none", "xs == ys"),
    ] {
        let source = program(ty, make, ask);
        let errors = check(source.as_bytes()).expect_err("a list of what has no Eq");
        assert!(
            errors.iter().any(|e| e.message.contains("as `Hidden` has no `Eq`")),
            "{errors:?} for\n{source}"
        );
    }
}

fn a_slice_is_written_as_a_list_is_alone_and_in_a_tuple_a_struct_and_an_option() {
    generate::check(
        4021,
        20,
        |g: &mut Gen| (0..g.below(5)).map(|_| g.below(300) as u64).collect::<Vec<u64>>(),
        |numbers| {
            let items: Vec<String> = numbers.iter().map(u64::to_string).collect();
            let text = format!("[{}]", items.join(", "));
            let program = with_header(format!(
                "struct Held {{\n  items: Slice[Nat]\n}}\n\nfn main() ! console, alloc {{\n  xs: Slice[Nat] = {text}\n  print(\"{{xs}} {{repr(xs)}}\")\n  t: (Slice[Nat], Nat) = (xs, 1)\n  print(\"{{t}}\")\n  h = Held(items: xs)\n  print(\"{{h}}\")\n  o: Option[Slice[Nat]] = .some(xs)\n  print(\"{{o}}\")\n}}\n"
            ));
            let want = format!(
                "{text} {text}\n({text}, 1)\nHeld(items: {text})\n.some({text})\n"
            );
            assert_eq!(stdout(&program), want, "for\n{program}");
        },
    );
}

fn a_list_is_written_as_the_page_says_where_a_struct_a_tuple_an_option_or_a_list_holds_it() {
    generate::check(4022, 60, reprs::program_with_lists, |case| {
        assert_eq!(
            stdout(&case.source).into_bytes(),
            case.stdout,
            "for\n{}",
            case.source
        );
    });
}

fn a_list_of_what_lacks_fmt_or_repr_is_refused_naming_the_element() {
    generate::check(4023, 60, reprs::program_with_lists_and_mistake, |case| {
        let expected = case.mistake.as_ref().expect("a placed mistake");
        let errors = check(case.source.as_bytes()).expect_err("a mistake");
        assert!(
            errors.iter().any(|e| e.message.contains(&expected.mentions)),
            "{errors:?} for\n{}",
            case.source
        );
    });
}
