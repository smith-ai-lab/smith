//! Ownership: a value whose type has no `Copy` is used at most once, a
//! parameter marked `take` owns its argument, and a lent value is never
//! kept. Each property takes what it expects from the generator that built
//! the program, which works out for itself where a value is given away and
//! where it is used again.

use std::collections::BTreeMap;

use smith::check;
use smith::diag::{Error, Pos};

use crate::Test;
use crate::generate::{self, Gen};
use crate::programs::pos_after;

pub const ONCE: &[Test] = &[
    (
        "the_examples_of_the_pages_on_ownership_and_take_are_accepted_or_refused_as_they_say",
        the_examples_of_the_pages_on_ownership_and_take_are_accepted_or_refused_as_they_say,
    ),
    (
        "a_type_has_copy_exactly_when_every_part_of_it_has_it_and_without_takes_it_away",
        a_type_has_copy_exactly_when_every_part_of_it_has_it_and_without_takes_it_away,
    ),
    (
        "a_value_without_copy_is_refused_exactly_where_it_is_used_after_it_was_given_away",
        a_value_without_copy_is_refused_exactly_where_it_is_used_after_it_was_given_away,
    ),
    (
        "a_lent_value_or_part_of_one_is_refused_where_it_is_kept_unless_it_has_copy",
        a_lent_value_or_part_of_one_is_refused_where_it_is_kept_unless_it_has_copy,
    ),
    (
        "a_function_type_marks_a_parameter_take_and_its_lambda_is_held_to_it",
        a_function_type_marks_a_parameter_take_and_its_lambda_is_held_to_it,
    ),
    (
        "what_could_spend_a_value_twice_through_a_lambda_or_a_type_parameter_is_refused_as_not_built",
        what_could_spend_a_value_twice_through_a_lambda_or_a_type_parameter_is_refused_as_not_built,
    ),
    (
        "the_two_readings_that_would_spend_a_key_twice_are_refused_as_not_built",
        the_two_readings_that_would_spend_a_key_twice_are_refused_as_not_built,
    ),
    (
        "without_copy_and_without_discard_are_refused_for_what_is_not_a_type_of_the_program",
        without_copy_and_without_discard_are_refused_for_what_is_not_a_type_of_the_program,
    ),
];

/// The errors a program gives, which are none when it is accepted.
fn errors(source: &str) -> Vec<Error> {
    match check(source.as_bytes()) {
        Ok(_) => Vec::new(),
        Err(errors) => errors,
    }
}

/// The text of a place, as `line:col`, which an error names where the value
/// was given away.
fn named(pos: Pos) -> String {
    format!("{}:{}", pos.line, pos.col)
}

// What the pages say.

/// A type `Key` that has no `Copy`, with what the examples of the pages
/// use, and the example's own functions for `split` and `uniform`.
const KEY: &str = "struct Key {
  secret: u64
}

without Copy for Key

fn split(k: take Key) -> (Key, Key) { (k, Key(secret: 2)) }

fn uniform(k: take Key) -> u64 { 3 }

";

fn the_examples_of_the_pages_on_ownership_and_take_are_accepted_or_refused_as_they_say() {
    // A number has `Copy`, so a struct of numbers has it.
    let mirror = "struct Point {
  x: u64
  y: u64
}

fn mirror(p: Point) -> Point {
  q = p
  Point(x: q.y, y: p.x)
}

fn main() {
  m = mirror(Point(x: 1, y: 2))
}
";
    assert!(errors(mirror).is_empty(), "{:?}", errors(mirror));

    // A struct that holds a `Key` is used at most once, and its number does
    // not change that.
    let seeded = format!(
        "{KEY}struct Seeded {{
  key: Key
  count: u64
}}

fn main() {{
  a = Seeded(key: Key(secret: 1), count: 2)
  b = a
  c = a
}}
"
    );
    let found = errors(&seeded);
    assert_eq!(found.len(), 1, "{found:?}");
    assert!(found[0].message.contains("`a`"), "{}", found[0]);

    // A lent value is not returned, and a `take` parameter owns what it is
    // given.
    let keep = |written: &str| {
        format!(
            "{KEY}fn keep(k: {written}Key) -> Key {{ k }}

fn main() {{
  a = keep(Key(secret: 1))
}}
"
        )
    };
    let refused = keep("");
    let found = errors(&refused);
    assert_eq!(found.len(), 1, "{found:?}");
    assert_eq!(found[0].pos, pos_after(&refused[..refused.find("{ k }").unwrap() + 2]));
    assert!(found[0].message.contains("lent"), "{}", found[0]);
    assert!(errors(&keep("take ")).is_empty());

    // A key that is split is spent, and each key it makes is used once.
    let numbers = |second: &str| {
        format!(
            "{KEY}fn numbers(key: take Key) -> (u64, u64) {{
  (k1, k2) = split(key)
  (uniform(k1), uniform({second}))
}}

fn main() {{
  n = numbers(Key(secret: 1))
}}
"
        )
    };
    assert!(errors(&numbers("k2")).is_empty());
    let twice = numbers("k1");
    let found = errors(&twice);
    assert_eq!(found.len(), 1, "{found:?}");
    let second = pos_after(&twice[..twice.rfind("k1").unwrap()]);
    let first = pos_after(&twice[..twice.find("uniform(k1)").unwrap() + "uniform(".len()]);
    assert_eq!(found[0].pos, second);
    assert!(found[0].message.contains(&named(first)), "{}", found[0]);
}

// Which types have `Copy`.

/// What a field or a case of a generated type holds.
#[derive(Clone, Debug)]
enum Part {
    Number,
    Text,
    /// A type that `without Copy` marks, which is not generated.
    Key,
    /// An earlier generated struct or enum.
    Earlier(usize),
    /// A tuple of two parts.
    Pair(Box<Part>, Box<Part>),
}

#[derive(Clone, Debug)]
struct Def {
    is_enum: bool,
    /// The fields of a struct, or the cases of an enum, each holding a part
    /// or nothing.
    parts: Vec<Option<Part>>,
    /// Whether `without Copy` is written for it.
    without: bool,
}

/// The types of a program, from the first, which holds only what is defined
/// before it, to the last, which the function takes.
#[derive(Debug)]
struct Types {
    defs: Vec<Def>,
}

impl Types {
    fn has_copy(&self, part: &Part) -> bool {
        match part {
            Part::Number | Part::Text => true,
            Part::Key => false,
            Part::Earlier(i) => self.def_has_copy(*i),
            Part::Pair(a, b) => self.has_copy(a) && self.has_copy(b),
        }
    }

    fn def_has_copy(&self, i: usize) -> bool {
        let def = &self.defs[i];
        !def.without && def.parts.iter().flatten().all(|p| self.has_copy(p))
    }

    /// The source of a part's type.
    fn show(part: &Part) -> String {
        match part {
            Part::Number => "u64".to_string(),
            Part::Text => "str".to_string(),
            Part::Key => "Key".to_string(),
            Part::Earlier(i) => format!("T{i}"),
            Part::Pair(a, b) => format!("({}, {})", Self::show(a), Self::show(b)),
        }
    }

    /// An expression for a value of a part's type.
    fn build(&self, part: &Part) -> String {
        match part {
            Part::Number => "1".to_string(),
            Part::Text => "\"a\"".to_string(),
            Part::Key => "Key(secret: 1)".to_string(),
            Part::Earlier(i) => self.build_def(*i),
            Part::Pair(a, b) => format!("({}, {})", self.build(a), self.build(b)),
        }
    }

    fn build_def(&self, i: usize) -> String {
        let def = &self.defs[i];
        if def.is_enum {
            match def.parts.iter().enumerate().next() {
                Some((c, Some(p))) => format!("T{i}.c{c}({})", self.build(p)),
                Some((c, None)) => format!("T{i}.c{c}"),
                None => unreachable!("an enum has a case"),
            }
        } else {
            let fields: Vec<String> = (def.parts.iter().enumerate())
                .map(|(f, p)| format!("f{f}: {}", self.build(p.as_ref().expect("a field"))))
                .collect();
            format!("T{i}({})", fields.join(", "))
        }
    }

    fn source(&self) -> String {
        let mut out = String::from("struct Key {\n  secret: u64\n}\n\nwithout Copy for Key\n\n");
        for (i, def) in self.defs.iter().enumerate() {
            if def.is_enum {
                out.push_str(&format!("enum T{i} {{\n"));
                for (c, p) in def.parts.iter().enumerate() {
                    match p {
                        Some(p) => out.push_str(&format!("  c{c}: {}\n", Self::show(p))),
                        None => out.push_str(&format!("  c{c}\n")),
                    }
                }
            } else {
                out.push_str(&format!("struct T{i} {{\n"));
                for (f, p) in def.parts.iter().enumerate() {
                    let p = p.as_ref().expect("a field");
                    out.push_str(&format!("  f{f}: {}\n", Self::show(p)));
                }
            }
            out.push_str("}\n\n");
            if def.without {
                out.push_str(&format!("without Copy for T{i}\n\n"));
            }
        }
        out
    }
}

fn part(g: &mut Gen, earlier: usize, depth: usize) -> Part {
    match g.below(if earlier == 0 { 4 } else { 5 }) {
        0 => Part::Number,
        1 => Part::Text,
        2 => Part::Key,
        3 if depth > 0 => Part::Pair(
            Box::new(part(g, earlier, depth - 1)),
            Box::new(part(g, earlier, depth - 1)),
        ),
        3 => Part::Number,
        _ => Part::Earlier(g.below(earlier)),
    }
}

fn types(g: &mut Gen) -> Types {
    let mut defs: Vec<Def> = Vec::new();
    for i in 0..1 + g.below(4) {
        let is_enum = g.one_in(2);
        let n = 1 + g.below(3);
        let parts = (0..n)
            .map(|c| {
                // An enum's first case may hold nothing, and a struct's
                // fields always hold something.
                if is_enum && g.one_in(3) {
                    None
                } else {
                    Some(part(g, i, 1))
                }
                .or_else(|| (!is_enum || c > 0).then_some(Part::Number))
            })
            .collect();
        defs.push(Def {
            is_enum,
            parts,
            without: g.one_in(5),
        });
    }
    Types { defs }
}

/// A function that uses its parameter twice, called with a value of the
/// last type, whose uses are refused exactly when the type lacks `Copy`.
fn twice_of_the_last(t: &Types) -> (String, Option<(Pos, Pos)>) {
    let last = t.defs.len() - 1;
    let mut source = t.source();
    source.push_str(&format!("fn f(v: take T{last}) -> u64 {{\n  a = "));
    let first = pos_after(&source);
    source.push_str("v\n  b = ");
    let second = pos_after(&source);
    source.push_str(&format!(
        "v\n  0\n}}\n\nfn main() {{\n  r = f({})\n}}\n",
        t.build_def(last)
    ));
    (source, (!t.def_has_copy(last)).then_some((first, second)))
}

fn a_type_has_copy_exactly_when_every_part_of_it_has_it_and_without_takes_it_away() {
    generate::check(700, 120, types, |t| {
        let (source, refused) = twice_of_the_last(t);
        let found = errors(&source);
        match refused {
            None => assert!(found.is_empty(), "{found:?}\n{source}"),
            Some((first, second)) => {
                assert_eq!(found.len(), 1, "{found:?}\n{source}");
                assert_eq!(found[0].pos, second, "{source}");
                assert!(found[0].message.contains(&named(first)), "{}", found[0]);
            }
        }
    });
}

// A value used after it was given away.

/// The types of a value the function takes, which a generated body works on.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Held {
    Key,
    Holder,
    Wrapper,
    Point,
    Shot,
}

impl Held {
    const ALL: [Held; 5] = [Held::Key, Held::Holder, Held::Wrapper, Held::Point, Held::Shot];

    fn word(self) -> &'static str {
        match self {
            Held::Key => "key",
            Held::Holder => "holder",
            Held::Wrapper => "wrapper",
            Held::Point => "point",
            Held::Shot => "shot",
        }
    }

    fn name(self) -> &'static str {
        match self {
            Held::Key => "Key",
            Held::Holder => "Holder",
            Held::Wrapper => "Wrapper",
            Held::Point => "Point",
            Held::Shot => "Shot",
        }
    }

    /// Whether the type has no `Copy`.
    fn lacks_copy(self) -> bool {
        matches!(self, Held::Key | Held::Holder | Held::Wrapper)
    }

    /// An expression for a value of the type.
    fn value(self) -> &'static str {
        match self {
            Held::Key => "Key(secret: 1)",
            Held::Holder => "Holder(key: Key(secret: 1), count: 2)",
            Held::Wrapper => "Wrapper.full(Key(secret: 1))",
            Held::Point => "Point(x: 1, y: 2)",
            Held::Shot => "Shot.hit(3)",
        }
    }
}

/// The types and the functions every generated program has. Each type has
/// a function that takes its value and one that lends it.
const PRELUDE: &str = "struct Key {
  secret: u64
}

without Copy for Key

struct Holder {
  key: Key
  count: u64
}

enum Wrapper {
  full: Key
  empty
}

struct Point {
  x: u64
  y: u64
}

enum Shot {
  hit: u64
  miss
}

enum Dir {
  north
  south
  east
}

fn sink_key(x: take Key) -> u64 { x.secret }

fn lend_key(x: Key) -> u64 { x.secret }

fn sink_holder(x: take Holder) -> u64 { x.count }

fn lend_holder(x: Holder) -> u64 { x.count }

fn sink_wrapper(x: take Wrapper) -> u64 {
  match x {
    .full(k) { sink_key(k) }
    .empty { 0 }
  }
}

fn lend_wrapper(x: Wrapper) -> u64 {
  match x {
    .full(k) { lend_key(k) }
    .empty { 0 }
  }
}

fn sink_point(x: take Point) -> u64 { x.x }

fn lend_point(x: Point) -> u64 { x.y }

fn sink_shot(x: take Shot) -> u64 {
  match x {
    .hit(h) { h }
    .miss { 0 }
  }
}

fn lend_shot(x: Shot) -> u64 {
  match x {
    .hit(h) { h }
    .miss { 0 }
  }
}

";

/// The part of the value a statement works on.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Place {
    Whole,
    /// The `key` of a `Holder`.
    Key,
    /// The `count` of a `Holder`.
    Count,
}

#[derive(Clone, Debug)]
struct Act {
    id: usize,
    give: bool,
    place: Place,
    /// Which way of giving or of looking, among those the type has.
    form: usize,
}

#[derive(Clone, Debug)]
enum Item {
    Act(Act),
    /// `if c { ... } else { ... }`, with no `else` for `None`.
    If(Vec<Item>, Option<Vec<Item>>),
    /// A `match` on a `Dir`, with a block for each of its three cases.
    Arms(Vec<Vec<Item>>),
    /// `loop i in 0..3 { ... }`.
    Loop(Vec<Item>),
    /// `if c { ... break }`, inside a loop.
    IfBreak(Vec<Item>),
}

/// How many ways there are to give a place, or to look at it.
fn forms(held: Held, give: bool, place: Place) -> usize {
    match (give, place) {
        (true, Place::Whole) => 4 + usize::from(matches!(held, Held::Wrapper | Held::Shot)),
        (true, Place::Key | Place::Count) => 1,
        (false, Place::Whole) => match held {
            Held::Key | Held::Point => 2,
            _ => 1,
        },
        (false, Place::Key | Place::Count) => 1,
    }
}

fn act(g: &mut Gen, held: Held, next: &mut usize) -> Item {
    let give = g.one_in(2);
    let place = match (held, g.below(3)) {
        (Held::Holder, 1) => Place::Key,
        (Held::Holder, 2) if !give => Place::Count,
        _ => Place::Whole,
    };
    let form = g.below(forms(held, give, place));
    *next += 1;
    Item::Act(Act {
        id: *next,
        give,
        place,
        form,
    })
}

fn items(g: &mut Gen, held: Held, depth: usize, in_loop: bool, next: &mut usize) -> Vec<Item> {
    (0..1 + g.below(3))
        .map(|_| match if depth == 0 { 0 } else { g.below(6) } {
            1 => Item::If(
                items(g, held, depth - 1, in_loop, next),
                g.one_in(2).then(|| items(g, held, depth - 1, in_loop, next)),
            ),
            2 => Item::Arms(
                (0..3)
                    .map(|_| items(g, held, depth - 1, in_loop, next))
                    .collect(),
            ),
            3 if !in_loop => Item::Loop(items(g, held, depth - 1, true, next)),
            4 if in_loop => Item::IfBreak(items(g, held, depth - 1, true, next)),
            _ => act(g, held, next),
        })
        .collect()
}

/// A generated body and the type of the value it works on.
#[derive(Clone, Debug)]
struct Body {
    held: Held,
    items: Vec<Item>,
    count: usize,
}

fn body(g: &mut Gen) -> Body {
    let held = *g.pick(&Held::ALL);
    let mut next = 0;
    let items = items(g, held, 2, false, &mut next);
    Body {
        held,
        items,
        count: next,
    }
}

/// A program, and where each statement's value is written.
struct Written {
    source: String,
    places: Vec<Pos>,
}

fn write_act(a: &Act, held: Held, out: &mut String, places: &mut [Pos], indent: &str) {
    let id = a.id;
    let t = held.word();
    let place = match a.place {
        Place::Whole => "v",
        Place::Key => "v.key",
        Place::Count => "v.count",
    };
    // Where the value is written is marked with `@`.
    let text = match (a.give, a.place, a.form) {
        (true, Place::Whole, 0) => format!("n{id} = sink_{t}(@)"),
        (true, Place::Whole, 1) => format!("n{id} = @"),
        (true, Place::Whole, 2) => format!("n{id} = (@, 1)"),
        (true, Place::Whole, 3) => format!("n{id} = Option.some(@)"),
        (true, Place::Whole, _) if held == Held::Wrapper => {
            format!("n{id} = match @ {{\n{indent}  .full(k{id}) {{ sink_key(k{id}) }}\n{indent}  .empty {{ 0 }}\n{indent}}}")
        }
        (true, Place::Whole, _) => {
            format!("n{id} = match @ {{\n{indent}  .hit(k{id}) {{ k{id} }}\n{indent}  .miss {{ 0 }}\n{indent}}}")
        }
        (true, ..) => format!("n{id} = sink_key(@)"),
        (false, Place::Whole, 0) => format!("n{id} = lend_{t}(@)"),
        (false, Place::Whole, _) if held == Held::Key => format!("n{id} = @.secret"),
        (false, Place::Whole, _) => format!("n{id} = @.x"),
        (false, Place::Key, _) => format!("n{id} = lend_key(@)"),
        (false, Place::Count, _) => format!("n{id} = @"),
    };
    let at = text.find('@').expect("a place is written");
    places[id - 1] = pos_after(&format!("{out}{indent}{}", &text[..at]));
    out.push_str(indent);
    out.push_str(&text.replacen('@', place, 1));
    out.push('\n');
}

fn write_items(
    items: &[Item],
    held: Held,
    out: &mut String,
    places: &mut [Pos],
    depth: usize,
    next_loop: &mut usize,
) {
    let indent = "  ".repeat(depth);
    let block = |items: &[Item], out: &mut String, places: &mut [Pos], next_loop: &mut usize, extra: &str| {
        write_items(items, held, out, places, depth + 1, next_loop);
        out.push_str(&format!("{indent}  {extra}0\n"));
    };
    for item in items {
        match item {
            Item::Act(a) => write_act(a, held, out, places, &indent),
            Item::If(then, otherwise) => {
                out.push_str(&format!("{indent}if c {{\n"));
                block(then, out, places, next_loop, "");
                match otherwise {
                    Some(otherwise) => {
                        out.push_str(&format!("{indent}}} else {{\n"));
                        block(otherwise, out, places, next_loop, "");
                        out.push_str(&format!("{indent}}}\n"));
                    }
                    None => out.push_str(&format!("{indent}}}\n")),
                }
            }
            Item::Arms(arms) => {
                out.push_str(&format!("{indent}match d {{\n"));
                for (arm, case) in arms.iter().zip(["north", "south", "east"]) {
                    out.push_str(&format!("{indent}  .{case} {{\n"));
                    write_items(arm, held, out, places, depth + 2, next_loop);
                    out.push_str(&format!("{indent}    0\n{indent}  }}\n"));
                }
                out.push_str(&format!("{indent}}}\n"));
            }
            Item::Loop(body) => {
                *next_loop += 1;
                out.push_str(&format!("{indent}loop i{} in 0..3 {{\n", *next_loop));
                block(body, out, places, next_loop, "");
                out.push_str(&format!("{indent}}}\n"));
            }
            Item::IfBreak(body) => {
                out.push_str(&format!("{indent}if c {{\n"));
                write_items(body, held, out, places, depth + 1, next_loop);
                out.push_str(&format!("{indent}  break\n{indent}}}\n"));
            }
        }
    }
}

fn write(b: &Body) -> Written {
    let mut source = String::from(PRELUDE);
    source.push_str(&format!(
        "fn f(v: take {}, c: bool, d: Dir) -> u64 {{\n",
        b.held.name()
    ));
    let mut places = vec![Pos { line: 0, col: 0 }; b.count];
    write_items(&b.items, b.held, &mut source, &mut places, 1, &mut 0);
    source.push_str(&format!(
        "  0\n}}\n\nfn main() {{\n  r = f({}, true, Dir.north)\n}}\n",
        b.held.value()
    ));
    Written { source, places }
}

/// Which places of the value are gone, each with where it was given away
/// on the paths that give it away: the whole value, and the `key` of a
/// `Holder`.
#[derive(Clone, Debug, Default, PartialEq)]
struct Gone {
    whole: Vec<usize>,
    key: Vec<usize>,
}

type Flow = Option<Gone>;

fn join(a: Flow, b: Flow) -> Flow {
    match (a, b) {
        (None, other) | (other, None) => other,
        (Some(mut a), Some(b)) => {
            for id in b.whole {
                if !a.whole.contains(&id) {
                    a.whole.push(id);
                }
            }
            for id in b.key {
                if !a.key.contains(&id) {
                    a.key.push(id);
                }
            }
            Some(a)
        }
    }
}

/// What the generator says the compiler must refuse: each use after a give
/// away, by the id of the statement, with the ids of the places that may
/// have given the value away.
struct Model {
    lacks: bool,
    refused: BTreeMap<usize, Vec<usize>>,
    breaks: Vec<Vec<Gone>>,
}

impl Model {
    fn run(&mut self, items: &[Item], flow: &mut Flow) {
        for item in items {
            let Some(gone) = flow else {
                return;
            };
            match item {
                Item::Act(a) => {
                    // A look at the count only meets a value given whole.
                    let mut conflict = gone.whole.clone();
                    if a.place != Place::Count {
                        conflict.extend(&gone.key);
                    }
                    if !conflict.is_empty() {
                        let known = self.refused.entry(a.id).or_default();
                        for id in conflict {
                            if !known.contains(&id) {
                                known.push(id);
                            }
                        }
                    } else if a.give && self.lacks {
                        match a.place {
                            Place::Whole => gone.whole.push(a.id),
                            _ => gone.key.push(a.id),
                        }
                    }
                }
                Item::If(then, otherwise) => {
                    let mut a = flow.clone();
                    self.run(then, &mut a);
                    let mut b = flow.clone();
                    if let Some(otherwise) = otherwise {
                        self.run(otherwise, &mut b);
                    }
                    *flow = join(a, b);
                }
                Item::Arms(arms) => {
                    let mut after = None;
                    for arm in arms {
                        let mut a = flow.clone();
                        self.run(arm, &mut a);
                        after = join(after, a);
                    }
                    *flow = after;
                }
                Item::Loop(body) => {
                    let start = flow.clone();
                    self.breaks.push(Vec::new());
                    let mut first = flow.clone();
                    self.run(body, &mut first);
                    let mut second = join(start.clone(), first.clone());
                    self.run(body, &mut second);
                    let mut after = join(start, join(first, second));
                    for b in self.breaks.pop().unwrap_or_default() {
                        after = join(after, Some(b));
                    }
                    *flow = after;
                }
                Item::IfBreak(body) => {
                    let mut taken = flow.clone();
                    self.run(body, &mut taken);
                    if let (Some(gone), Some(exits)) = (taken, self.breaks.last_mut()) {
                        exits.push(gone);
                    }
                }
            }
        }
    }
}

fn the_refusals_of(b: &Body) -> BTreeMap<usize, Vec<usize>> {
    let mut model = Model {
        lacks: b.held.lacks_copy(),
        refused: BTreeMap::new(),
        breaks: Vec::new(),
    };
    model.run(&b.items, &mut Some(Gone::default()));
    model.refused
}

fn a_value_without_copy_is_refused_exactly_where_it_is_used_after_it_was_given_away() {
    generate::check(701, 150, body, |b| {
        let written = write(b);
        let found = errors(&written.source);
        let refused = the_refusals_of(b);
        let mut places: Vec<Pos> = refused.keys().map(|id| written.places[id - 1]).collect();
        places.sort();
        let got: Vec<Pos> = found.iter().map(|e| e.pos).collect();
        assert_eq!(got, places, "{found:?}\n{}", written.source);
        for e in &found {
            let id = refused
                .keys()
                .find(|id| written.places[**id - 1] == e.pos)
                .expect("a refusal of the model");
            assert!(
                refused[id]
                    .iter()
                    .any(|g| e.message.contains(&named(written.places[g - 1]))),
                "{e} names none of {:?}\n{}",
                refused[id],
                written.source
            );
        }
    });
}

// A lent value.

/// Where a lent value goes.
#[derive(Clone, Copy, Debug)]
enum Keeps {
    Returned,
    /// Part of what is returned, as the `key` of a `Holder` is.
    PartReturned,
    Stored,
    Taken,
    PartTaken,
    /// Bound to a name, which is then returned.
    Aliased,
    /// Only looked at.
    Lent,
}

#[derive(Clone, Debug)]
struct Lends {
    held: Held,
    keeps: Keeps,
    take: bool,
}

fn lends(g: &mut Gen) -> Lends {
    let held = *g.pick(&Held::ALL);
    let all = [
        Keeps::Lent,
        Keeps::Returned,
        Keeps::Taken,
        Keeps::Aliased,
        Keeps::Stored,
        Keeps::PartReturned,
        Keeps::PartTaken,
    ];
    let keeps = loop {
        let keeps = *g.pick(&all);
        let fits = match keeps {
            Keeps::Stored => held == Held::Key,
            Keeps::PartReturned | Keeps::PartTaken => held == Held::Holder,
            _ => true,
        };
        if fits {
            break keeps;
        }
    };
    Lends {
        held,
        keeps,
        take: g.one_in(3),
    }
}

fn a_lent_value_or_part_of_one_is_refused_where_it_is_kept_unless_it_has_copy() {
    generate::check(702, 120, lends, |l| {
        let t = l.held.name();
        let w = l.held.word();
        let taking = if l.take { "take " } else { "" };
        let (result, text) = match l.keeps {
            Keeps::Returned => (t.to_string(), "@".to_string()),
            Keeps::PartReturned => ("Key".to_string(), "@.key".to_string()),
            Keeps::Stored => ("Holder".to_string(), "Holder(key: @, count: 1)".to_string()),
            Keeps::Taken => ("u64".to_string(), format!("sink_{w}(@)")),
            Keeps::PartTaken => ("u64".to_string(), "sink_key(@.key)".to_string()),
            Keeps::Aliased => (t.to_string(), "q = v\n  @".to_string()),
            Keeps::Lent => ("u64".to_string(), format!("lend_{w}(@)")),
        };
        // The value is written where `@` is, and for an alias it is `q`.
        let place = if matches!(l.keeps, Keeps::Aliased) { "q" } else { "v" };
        let mut source = String::from(PRELUDE);
        source.push_str(&format!("fn g(v: {taking}{t}) -> {result} {{\n  "));
        let at = text.find('@').unwrap();
        let wanted = pos_after(&format!("{source}{}", &text[..at]));
        source.push_str(&text.replacen('@', place, 1));
        source.push_str(&format!(
            "\n}}\n\nfn main() {{\n  r = g({})\n}}\n",
            l.held.value()
        ));
        let found = errors(&source);
        let refused = !l.take && l.held.lacks_copy() && !matches!(l.keeps, Keeps::Lent);
        if refused {
            assert_eq!(found.len(), 1, "{found:?}\n{source}");
            assert_eq!(found[0].pos, wanted, "{source}");
            assert!(found[0].message.contains("lent"), "{}", found[0]);
        } else {
            assert!(found.is_empty(), "{found:?}\n{source}");
        }
    });
}

// A function type that marks a parameter `take`.

#[derive(Clone, Debug)]
struct Lambda {
    /// Whether the function type marks its parameter `take`.
    marked: bool,
    /// What the lambda does with its parameter: returns it, gives it to a
    /// function that takes it, or only lends it.
    does: usize,
    /// Whether the function that takes the lambda calls it twice.
    twice: bool,
}

fn lambdas(g: &mut Gen) -> Lambda {
    Lambda {
        marked: g.one_in(2),
        does: g.below(3),
        twice: g.one_in(2),
    }
}

fn a_function_type_marks_a_parameter_take_and_its_lambda_is_held_to_it() {
    generate::check(703, 80, lambdas, |l| {
        let marked = if l.marked { "take " } else { "" };
        let (result, lambda) = match l.does {
            0 => ("Key", "x"),
            1 => ("u64", "sink_key(x)"),
            _ => ("u64", "lend_key(x)"),
        };
        // A key cannot be added, so a function that gives one calls once.
        let twice = l.twice && result == "u64";
        let mut source = String::from(PRELUDE);
        source.push_str(&format!(
            "fn apply(k: take Key, f: ({marked}Key) -> {result}) -> {result} {{\n  "
        ));
        source.push_str(if twice { "n = f(k)\n  f(" } else { "f(" });
        let mut wanted: Vec<Pos> = Vec::new();
        // The function takes its argument, so a second call has none.
        if twice && l.marked {
            wanted.push(pos_after(&source));
        }
        source.push_str("k)");
        source.push_str("\n}\n\nfn main() {\n  r = apply(Key(secret: 1)) fn(x) { ");
        // The lambda owns its parameter only when the type says so.
        if !l.marked && l.does != 2 {
            wanted.push(pos_after(&format!(
                "{source}{}",
                &lambda[..lambda.find('x').unwrap()]
            )));
        }
        source.push_str(&format!("{lambda} }}\n}}\n"));
        wanted.sort();
        let found = errors(&source);
        let got: Vec<Pos> = found.iter().map(|e| e.pos).collect();
        assert_eq!(got, wanted, "{found:?}\n{source}");
    });
}

fn without_copy_and_without_discard_are_refused_for_what_is_not_a_type_of_the_program() {
    // A type of the program has either taken away, and a trait the compiler
    // gives no such thing to is refused as before.
    let ok = "struct Key {
  secret: u64
}

without Copy for Key
without Discard for Key

fn main() {
  k = Key(secret: 1)
}
";
    assert!(errors(ok).is_empty(), "{:?}", errors(ok));
    for (line, what) in [
        ("without Copy for Nope", "no type named `Nope`"),
        ("without Discard for Nope", "no type named `Nope`"),
        ("without Copy for str", "is neither"),
        ("without Copy for Key\nwithout Copy for Key", "already taken away"),
    ] {
        let source = format!("struct Key {{\n  secret: u64\n}}\n\n{line}\n\nfn main() {{\n  k = Key(secret: 1)\n}}\n");
        let found = errors(&source);
        assert_eq!(found.len(), 1, "{found:?}\n{source}");
        assert!(found[0].message.contains(what), "{}\n{source}", found[0]);
    }
}

// What is not built yet, because it could spend a value twice.

/// What the shapes below call: a function that calls its argument twice, and
/// functions over a type parameter, which may copy what they are given, and
/// an implementation for a type that holds one.
const GENERIC: &str = "fn twice(f: () -> u64) -> u64 {
  a = f()
  f()
}

fn dup[T](x: T) -> (T, T) { (x, x) }

fn first[T, U](x: T, y: U) -> T { x }

struct Box[T] {
  item: T
}

trait Named[T] {
  fn name_of(x: T) -> u64
}

impl[T] Named[Box[T]] {
  fn name_of(x: Box[T]) -> u64 { 1 }
}

";

/// A statement of `main`, where `@` is where the one error must be, if the
/// value has no `Copy`, and `W` is the word of the value's type. The last
/// ones are accepted for every type.
const SHAPES: [(&str, bool); 9] = [
    ("r = twice({ sink_W(@v) })", true),
    ("r = twice({ lend_W(v) })", false),
    ("r = twice({\n    q = VALUE\n    sink_W(q)\n  })", false),
    ("(a, b) = @dup(v)", true),
    ("r = @first(v, 1)", true),
    ("r = @first(1, v)", true),
    ("r = @name_of(Box(item: v))", true),
    ("o = Option.some(v)", false),
    ("b = Box(item: v)", false),
];

fn what_could_spend_a_value_twice_through_a_lambda_or_a_type_parameter_is_refused_as_not_built() {
    generate::check(
        704,
        100,
        |g| (*g.pick(&Held::ALL), g.below(SHAPES.len())),
        |(held, shape)| {
            let (text, refused) = SHAPES[*shape];
            let text = text.replace('W', held.word()).replace("VALUE", held.value());
            let mut source = String::from(PRELUDE);
            source.push_str(GENERIC);
            source.push_str(&format!("fn main() {{\n  v = {}\n  ", held.value()));
            let at = text.find('@').map(|at| pos_after(&format!("{source}{}", &text[..at])));
            source.push_str(&text.replace('@', ""));
            source.push_str("\n}\n");
            let found = errors(&source);
            match at {
                Some(at) if refused && held.lacks_copy() => {
                    assert_eq!(found.len(), 1, "{found:?}\n{source}");
                    assert_eq!(found[0].pos, at, "{source}");
                    assert!(found[0].message.contains("not supported yet"), "{}", found[0]);
                }
                _ => assert!(found.is_empty(), "{found:?}\n{source}"),
            }
        },
    );
}

fn the_two_readings_that_would_spend_a_key_twice_are_refused_as_not_built() {
    for (body, what) in [
        ("r = twice({ spend(k) })", "inside a lambda"),
        ("(a, b) = dup(k)", "the type parameter `T` of `dup`"),
    ] {
        let source = format!(
            "{KEY}fn spend(k: take Key) -> u64 {{ k.secret }}

fn twice(f: () -> u64) -> u64 {{
  a = f()
  f()
}}

fn dup[T](x: T) -> (T, T) {{ (x, x) }}

fn main() {{
  k = Key(secret: 5)
  {body}
}}
"
        );
        let found = errors(&source);
        assert_eq!(found.len(), 1, "{found:?}\n{source}");
        assert!(found[0].message.contains(what), "{}", found[0]);
        assert!(found[0].message.contains("not supported yet"), "{}", found[0]);
    }
}
