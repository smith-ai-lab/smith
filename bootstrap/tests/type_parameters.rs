//! A type parameter stands for any type, and nothing is known of it but what
//! its bounds say: a value of one is used at most once and must be used,
//! unless `Copy` lets it be used again and `Discard` lets it be dropped. A
//! call gives a type parameter only a type that has what its bounds ask.
//!
//! The generators know which traits each type has and which a function asks
//! for, and work out for themselves where each value is given away, used
//! again, or dropped, and so where the one error of each mistake must be.

use std::collections::BTreeSet;

use smith::check;
use smith::diag::{Error, Pos};
use smith::{lower, scalar};

use crate::Test;
use crate::generate::{self, Gen};
use crate::programs::pos_after;

pub const ONCE: &[Test] = &[
    (
        "a_generic_function_that_drops_or_copies_a_value_says_so_in_a_bound",
        a_generic_function_that_drops_or_copies_a_value_says_so_in_a_bound,
    ),
    (
        "a_generic_function_that_only_hands_on_or_reads_needs_no_bound_and_takes_a_key",
        a_generic_function_that_only_hands_on_or_reads_needs_no_bound_and_takes_a_key,
    ),
    (
        "a_call_that_gives_a_type_without_what_a_bound_asks_is_one_error_at_the_call",
        a_call_that_gives_a_type_without_what_a_bound_asks_is_one_error_at_the_call,
    ),
    (
        "a_value_of_a_type_parameter_is_refused_exactly_where_it_is_used_again_or_dropped_and_each_call_where_a_type_lacks_a_bound",
        a_value_of_a_type_parameter_is_refused_exactly_where_it_is_used_again_or_dropped_and_each_call_where_a_type_lacks_a_bound,
    ),
    (
        "a_value_of_a_type_parameter_is_returned_or_stored_when_taken_and_when_lent_only_with_copy",
        a_value_of_a_type_parameter_is_returned_or_stored_when_taken_and_when_lent_only_with_copy,
    ),
    (
        "an_implementation_is_chosen_for_a_type_without_copy_unless_it_asks_for_it",
        an_implementation_is_chosen_for_a_type_without_copy_unless_it_asks_for_it,
    ),
];

/// The errors a program gives, which are none when it is accepted.
fn errors(source: &str) -> Vec<Error> {
    match check(source.as_bytes()) {
        Ok(_) => Vec::new(),
        Err(errors) => errors,
    }
}

/// The types a type parameter is given. `Key` has `Discard` and not `Copy`,
/// `Soft` has `Copy` and not `Discard`, and `Raw` has neither.
const TYPES: &str = "struct Key {
  secret: u64
}

without Copy for Key

struct Soft {
  n: u64
}

without Discard for Soft

struct Raw {
  n: u64
}

without Copy for Raw
without Discard for Raw

struct Box[T] {
  item: T
}

enum Dir {
  north
  south
  east
}

";

/// A type that a type parameter is given, with what it has.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Given {
    Number,
    Key,
    Soft,
    Raw,
}

impl Given {
    const ALL: [Given; 4] = [Given::Number, Given::Key, Given::Soft, Given::Raw];

    fn name(self) -> &'static str {
        match self {
            Given::Number => "u64",
            Given::Key => "Key",
            Given::Soft => "Soft",
            Given::Raw => "Raw",
        }
    }

    fn copy(self) -> bool {
        matches!(self, Given::Number | Given::Soft)
    }

    fn discard(self) -> bool {
        matches!(self, Given::Number | Given::Key)
    }

    /// The statement that makes a value named `v`.
    fn define(self) -> &'static str {
        match self {
            Given::Number => "v: u64 = 1",
            Given::Key => "v = Key(secret: 1)",
            Given::Soft => "v = Soft(n: 1)",
            Given::Raw => "v = Raw(n: 1)",
        }
    }
}

/// Where a parameter's type holds the type parameter `T`, and how a value of
/// it is made from a value of the type `T` stands for.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Shape {
    Plain,
    Struct,
    Tuple,
    Enum,
    List,
}

impl Shape {
    const ALL: [Shape; 5] = [
        Shape::Plain,
        Shape::Struct,
        Shape::Tuple,
        Shape::Enum,
        Shape::List,
    ];

    /// The type of a value that holds `inner`.
    fn ty(self, inner: &str) -> String {
        match self {
            Shape::Plain => inner.to_string(),
            Shape::Struct => format!("Box[{inner}]"),
            Shape::Tuple => format!("({inner}, u64)"),
            Shape::Enum => format!("Option[{inner}]"),
            Shape::List => format!("List[{inner}]"),
        }
    }

    /// The statement that names a list of `inner`, which a list is written
    /// as, and what the value is then called.
    fn named(self, inner: &str, ty: &str) -> (String, String) {
        match self {
            Shape::List => (
                format!("  l: List[{ty}] = [{inner}]\n"),
                "l".to_string(),
            ),
            other => (String::new(), other.make(inner)),
        }
    }

    /// A value that holds `inner`.
    fn make(self, inner: &str) -> String {
        match self {
            Shape::Plain => inner.to_string(),
            Shape::Struct => format!("Box(item: {inner})"),
            Shape::Tuple => format!("({inner}, 1)"),
            Shape::Enum => format!("Option.some({inner})"),
            Shape::List => format!("[{inner}]"),
        }
    }
}

/// What a function says of its type parameter.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct Asks {
    copy: bool,
    discard: bool,
    /// Whether it is written as `requires` lines and not as a bound.
    requires: bool,
}

impl Asks {
    /// The type parameter list and what follows the signature.
    fn written(self) -> (String, String) {
        let bound = match (self.copy, self.discard, self.requires) {
            (false, false, _) => String::new(),
            (true, false, false) => ": Copy".to_string(),
            (false, true, false) => ": Discard".to_string(),
            _ => String::new(),
        };
        let mut lines = String::new();
        if self.requires || (self.copy && self.discard) {
            if self.copy {
                lines.push_str("\n  requires Copy[T]");
            }
            if self.discard {
                lines.push_str("\n  requires Discard[T]");
            }
        }
        (format!("[T{bound}]"), lines)
    }
}

fn asks(g: &mut Gen) -> Asks {
    Asks {
        copy: g.one_in(2),
        discard: g.one_in(2),
        requires: g.one_in(2),
    }
}

/// The functions every generated program has, which only the bodies reach.
const HELPERS: &str = "fn lend[T](x: T) -> u64 { 1 }

fn eat[T: Discard](x: take T) -> u64 { 1 }

fn twin[T: Copy](x: T) -> u64 { 1 }

fn size_box[T](b: Box[T]) -> u64 { 1 }

fn pass[T](x: take T) -> T { x }

";

/// What a statement of the body does with the value `a`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Form {
    /// Lends it to a function with no bound.
    Look,
    /// Gives it to a function that takes it and asks for `Discard`.
    Eat,
    /// Lends it to a function that asks for `Copy`.
    Twin,
    /// Gives it away inside a struct.
    Store,
    /// Gives it to a function that gives it back to a name nothing uses.
    Pass,
}

#[derive(Clone, Debug)]
struct Act {
    id: usize,
    form: Form,
}

#[derive(Clone, Debug)]
enum Item {
    Act(Act),
    If(Vec<Item>, Option<Vec<Item>>),
    Arms(Vec<Vec<Item>>),
    Loop(Vec<Item>),
}

fn items(g: &mut Gen, depth: usize, next: &mut usize) -> Vec<Item> {
    (0..1 + g.below(3))
        .map(|_| match if depth == 0 { 0 } else { g.below(5) } {
            1 => Item::If(
                items(g, depth - 1, next),
                g.one_in(2).then(|| items(g, depth - 1, next)),
            ),
            2 => Item::Arms((0..3).map(|_| items(g, depth - 1, next)).collect()),
            3 => Item::Loop(items(g, depth - 1, next)),
            _ => {
                *next += 1;
                let form = *g.pick(&[Form::Look, Form::Eat, Form::Twin, Form::Store, Form::Pass]);
                Item::Act(Act { id: *next, form })
            }
        })
        .collect()
}

/// What may be true at one point of the body: that the value is gone, and
/// that it was not used on some path that reaches it.
#[derive(Clone, Copy, Debug, Default)]
struct State {
    gone: bool,
    unused: bool,
}

fn join(a: State, b: State) -> State {
    State {
        gone: a.gone || b.gone,
        unused: a.unused || b.unused,
    }
}

/// What the generator says the compiler must refuse.
struct Model {
    copy: bool,
    /// The statements that use the value after it was given away.
    again: BTreeSet<usize>,
    /// The statements whose call gives a function a type without what its
    /// bound asks.
    lacking: BTreeSet<usize>,
    /// The statements that bind a value that nothing uses.
    unused: BTreeSet<usize>,
    discard: bool,
}

impl Model {
    fn run(&mut self, items: &[Item], mut st: State) -> State {
        for item in items {
            match item {
                Item::Act(a) => {
                    if st.gone {
                        self.again.insert(a.id);
                    } else if matches!(a.form, Form::Eat | Form::Store | Form::Pass) && !self.copy {
                        st.gone = true;
                    }
                    st.unused = false;
                    let asked = match a.form {
                        Form::Eat => !self.discard,
                        Form::Twin => !self.copy,
                        Form::Look | Form::Store | Form::Pass => false,
                    };
                    if a.form == Form::Pass && !self.discard {
                        self.unused.insert(a.id);
                    }
                    if asked {
                        self.lacking.insert(a.id);
                    }
                }
                Item::If(then, otherwise) => {
                    let a = self.run(then, st);
                    let b = match otherwise {
                        Some(otherwise) => self.run(otherwise, st),
                        None => st,
                    };
                    st = join(a, b);
                }
                Item::Arms(arms) => {
                    let mut after = self.run(&arms[0], st);
                    for arm in &arms[1..] {
                        after = join(after, self.run(arm, st));
                    }
                    st = after;
                }
                Item::Loop(body) => {
                    let first = self.run(body, st);
                    let second = self.run(body, join(st, first));
                    st = join(st, join(first, second));
                }
            }
        }
        st
    }
}

#[derive(Clone, Debug)]
struct Case {
    shape: Shape,
    asks: Asks,
    items: Vec<Item>,
    given: Given,
}

fn case(g: &mut Gen) -> Case {
    let shape = *g.pick(&Shape::ALL);
    let given = *g.pick(&Given::ALL);
    let asks = asks(g);
    let items = items(g, 2, &mut 0);
    Case {
        shape,
        asks,
        items,
        given,
    }
}

/// The program, and where each place that may be refused is.
struct Written {
    source: String,
    /// The parameter, where a value that may be dropped is reported.
    param: Pos,
    /// Where each statement reads the value, and where its call is.
    reads: Vec<Pos>,
    calls: Vec<Pos>,
    /// Where each statement names what it binds.
    names: Vec<Pos>,
    /// Where `main` calls the function.
    main: Pos,
}

fn write_items(items: &[Item], out: &mut String, w: &mut Written, depth: usize, loops: &mut usize) {
    let indent = "  ".repeat(depth);
    for item in items {
        match item {
            Item::Act(a) => {
                let (call, value) = match a.form {
                    Form::Look => ("lend(", "a)"),
                    Form::Eat => ("eat(", "a)"),
                    Form::Twin => ("twin(", "a)"),
                    Form::Store => ("size_box(Box(item: ", "a))"),
                    Form::Pass => ("pass(", "a)"),
                };
                out.push_str(indent.as_str());
                w.names[a.id - 1] = pos_after(out);
                out.push_str(&format!("n{} = ", a.id));
                w.calls[a.id - 1] = pos_after(out);
                out.push_str(call);
                w.reads[a.id - 1] = pos_after(out);
                out.push_str(value);
                out.push('\n');
            }
            Item::If(then, otherwise) => {
                out.push_str(&format!("{indent}if c {{\n"));
                write_items(then, out, w, depth + 1, loops);
                out.push_str(&format!("{indent}  0\n"));
                match otherwise {
                    Some(otherwise) => {
                        out.push_str(&format!("{indent}}} else {{\n"));
                        write_items(otherwise, out, w, depth + 1, loops);
                        out.push_str(&format!("{indent}  0\n{indent}}}\n"));
                    }
                    None => out.push_str(&format!("{indent}}}\n")),
                }
            }
            Item::Arms(arms) => {
                out.push_str(&format!("{indent}match d {{\n"));
                for (arm, case) in arms.iter().zip(["north", "south", "east"]) {
                    out.push_str(&format!("{indent}  .{case} {{\n"));
                    write_items(arm, out, w, depth + 2, loops);
                    out.push_str(&format!("{indent}    0\n{indent}  }}\n"));
                }
                out.push_str(&format!("{indent}}}\n"));
            }
            Item::Loop(body) => {
                *loops += 1;
                out.push_str(&format!("{indent}loop i{} in 0..3 {{\n", *loops));
                write_items(body, out, w, depth + 1, loops);
                out.push_str(&format!("{indent}}}\n"));
            }
        }
    }
}

fn count(items: &[Item]) -> usize {
    items
        .iter()
        .map(|item| match item {
            Item::Act(_) => 1,
            Item::If(then, otherwise) => {
                count(then) + otherwise.as_ref().map_or(0, |o| count(o))
            }
            Item::Arms(arms) => arms.iter().map(|a| count(a)).sum(),
            Item::Loop(body) => count(body),
        })
        .sum()
}

fn write(c: &Case) -> Written {
    let n = count(&c.items);
    let mut w = Written {
        source: String::new(),
        param: Pos { line: 0, col: 0 },
        reads: vec![Pos { line: 0, col: 0 }; n],
        calls: vec![Pos { line: 0, col: 0 }; n],
        names: vec![Pos { line: 0, col: 0 }; n],
        main: Pos { line: 0, col: 0 },
    };
    let mut out = format!("{TYPES}{HELPERS}");
    let (generics, lines) = c.asks.written();
    out.push_str(&format!("fn f{generics}("));
    w.param = pos_after(&out);
    out.push_str(&format!(
        "a: take {}, c: bool, d: Dir) -> u64{lines}\n{{\n",
        c.shape.ty("T")
    ));
    write_items(&c.items, &mut out, &mut w, 1, &mut 0);
    let (list, value) = c.shape.named("v", c.given.name());
    let effect = if list.is_empty() { "" } else { " ! alloc" };
    out.push_str(&format!(
        "  0\n}}\n\nfn main(){effect} {{\n  {}\n{list}  r = ",
        c.given.define()
    ));
    w.main = pos_after(&out);
    out.push_str(&format!("f({value}, true, Dir.north)\n}}\n"));
    w.source = out;
    w
}

fn a_value_of_a_type_parameter_is_refused_exactly_where_it_is_used_again_or_dropped_and_each_call_where_a_type_lacks_a_bound()
 {
    generate::check(2601, 100, case, |c| {
        let w = write(c);
        let found = errors(&w.source);
        let mut model = Model {
            copy: c.asks.copy,
            again: BTreeSet::new(),
            lacking: BTreeSet::new(),
            unused: BTreeSet::new(),
            discard: c.asks.discard,
        };
        let end = model.run(
            &c.items,
            State {
                gone: false,
                unused: true,
            },
        );
        // What the function does, as the bounds of its parameter allow.
        let mut wanted: Vec<Pos> = model.again.iter().map(|id| w.reads[id - 1]).collect();
        wanted.extend(model.lacking.iter().map(|id| w.calls[id - 1]));
        wanted.extend(model.unused.iter().map(|id| w.names[id - 1]));
        if end.unused && !c.asks.discard {
            wanted.push(w.param);
        }
        // The call in `main`, which gives the type what the bounds ask.
        let missing = (c.asks.copy && !c.given.copy()) || (c.asks.discard && !c.given.discard());
        if missing {
            wanted.push(w.main);
        }
        wanted.sort();
        let got: Vec<Pos> = found.iter().map(|e| e.pos).collect();
        assert_eq!(got, wanted, "{found:?}\n{}", w.source);
        for e in &found {
            if e.pos == w.main {
                assert!(e.message.contains(c.given.name()), "{e}\n{}", w.source);
            } else if e.pos == w.param {
                assert!(
                    e.fix.as_deref().is_some_and(|f| f.contains("Discard")),
                    "{e:?}\n{}",
                    w.source
                );
            } else if w.reads.contains(&e.pos) {
                assert!(
                    e.fix.as_deref().is_some_and(|f| f.contains("Copy")),
                    "{e:?}\n{}",
                    w.source
                );
            }
        }
    });
}

// What the issue says, as it says it.

fn a_generic_function_that_drops_or_copies_a_value_says_so_in_a_bound() {
    let program = |first: &str, twice: &str, tail: &str| {
        format!(
            "{TYPES}fn first{first}(a: take T, b: take T) -> T {{ a }}

fn twice{twice}(a: take T) -> (T, T) {{ (a, a) }}

fn main() {{
  x = first(1, 2)
  (y, z) = twice(3)
{tail}}}
"
        )
    };
    let refused = program("[T]", "[T]", "");
    let found = errors(&refused);
    assert_eq!(found.len(), 2, "{found:?}");
    let dropped = pos_after(&refused[..refused.find("b: take").unwrap()]);
    let again = pos_after(&refused[..refused.find("(a, a)").unwrap() + 4]);
    assert_eq!(found[0].pos, dropped);
    assert!(found[0].message.contains("`b`"), "{}", found[0]);
    assert!(found[0].fix.as_deref().unwrap().contains("Discard"));
    assert_eq!(found[1].pos, again);
    assert!(found[1].fix.as_deref().unwrap().contains("Copy"));
    let accepted = program("[T: Discard]", "[T: Copy]", "");
    assert!(errors(&accepted).is_empty(), "{:?}", errors(&accepted));
}

fn a_generic_function_that_only_hands_on_or_reads_needs_no_bound_and_takes_a_key() {
    let source = format!(
        "{TYPES}fn keep[T](a: take T) -> T {{ a }}

fn size_of[T](xs: List[T]) -> Nat {{ xs.size }}

fn main() ! console, alloc {{
  k = keep(Key(secret: 4))
  ks: List[Key] = [Key(secret: 1), Key(secret: 2)]
  n = size_of(ks)
  m = size_of(ks)
  print(\"{{k.secret}} {{n}} {{m}}\")
}}
"
    );
    let found = errors(&source);
    assert!(found.is_empty(), "{found:?}\n{source}");
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
    assert_eq!(scalar::run(&program).stdout, b"4 2 2\n");
}

fn a_call_that_gives_a_type_without_what_a_bound_asks_is_one_error_at_the_call() {
    for (bound, given) in [("Copy", Given::Key), ("Discard", Given::Soft), ("Copy", Given::Raw)] {
        let mut source = format!(
            "{TYPES}fn use_it[T: {bound}](a: T) -> u64 {{ 1 }}

fn main() {{
  {}
  n = ",
            given.define()
        );
        let at = pos_after(&source);
        source.push_str("use_it(v)\n  m = n\n}\n");
        let found = errors(&source);
        assert_eq!(found.len(), 1, "{found:?}\n{source}");
        assert_eq!(found[0].pos, at, "{source}");
        assert!(found[0].message.contains(given.name()), "{}", found[0]);
        assert!(found[0].message.contains(bound), "{}", found[0]);
        assert!(found[0].message.contains("`use_it`"), "{}", found[0]);
    }
}

// A value that is returned or stored.

fn a_value_of_a_type_parameter_is_returned_or_stored_when_taken_and_when_lent_only_with_copy() {
    let generate = |g: &mut Gen| (*g.pick(&Shape::ALL), g.one_in(2), asks(g));
    generate::check(2602, 60, generate, |(shape, take, asks)| {
        let (generics, lines) = asks.written();
        let taking = if *take { "take " } else { "" };
        let effect = if *shape == Shape::List { " ! alloc" } else { "" };
        let mut source = format!(
            "{TYPES}fn g{generics}(a: {taking}T) -> {}{effect}{lines}\n{{\n  ",
            shape.ty("T")
        );
        let (list, value) = match shape {
            Shape::List => ("l: List[T] = [a]\n  ".to_string(), "l".to_string()),
            other => (String::new(), other.make("a")),
        };
        let text = if list.is_empty() { &value } else { &list };
        // The place of the `a` in the value that is built.
        let at = pos_after(&format!("{source}{}", &text[..text.find('a').unwrap()]));
        source.push_str(&list);
        source.push_str(&value);
        source.push_str(&format!(
            "\n}}\n\nfn main(){effect} {{\n  v: u64 = 1\n  r = g(v)\n  s = r\n}}\n"
        ));
        let found = errors(&source);
        // A value that is lent may be kept only if it has `Copy`, and one
        // that is taken is given away once.
        if !*take && !asks.copy {
            assert_eq!(found.len(), 1, "{found:?}\n{source}");
            assert_eq!(found[0].pos, at, "{source}");
            assert!(found[0].message.contains("lent"), "{}", found[0]);
        } else {
            assert!(found.is_empty(), "{found:?}\n{source}");
        }
    });
}

// An implementation.

fn an_implementation_is_chosen_for_a_type_without_copy_unless_it_asks_for_it() {
    let program = |head: &str, given: Given| {
        format!(
            "{TYPES}trait Named[T] {{
  fn name_of(x: T) -> u64
}}

impl{head} Named[Box[T]] {{
  fn name_of(x: Box[T]) -> u64 {{ 1 }}
}}

fn main() {{
  {}
  n = name_of(Box(item: v))
  m = n
}}
",
            given.define()
        )
    };
    for given in Given::ALL {
        let free = program("[T]", given);
        assert!(errors(&free).is_empty(), "{:?}\n{free}", errors(&free));
        let asked = program("[T: Copy]", given);
        let found = errors(&asked);
        if given.copy() {
            assert!(found.is_empty(), "{found:?}\n{asked}");
        } else {
            assert_eq!(found.len(), 1, "{found:?}\n{asked}");
            assert!(found[0].message.contains("Copy"), "{}", found[0]);
        }
    }
}
