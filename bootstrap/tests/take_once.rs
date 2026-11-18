//! A value whose type has neither `Copy` nor `Discard` is taken exactly once
//! on every path, so that a resource cannot be forgotten.
//!
//! The generator builds a function over one value, of a type with `Discard`
//! or without it, held by a parameter, a binding, or a `var`, in each kind of
//! type that holds another: a struct, a tuple, an enum, and a list. The body
//! lends it, reads a field of it, gives it away, takes an enum apart, hands it
//! to a lambda, assigns the `var` again, and leaves by `return`, by a `try`, or
//! by `break`, inside an `if`, a `match`, and a `loop`.
//!
//! The expectation is worked out by running every path of the body, with a
//! loop going round none to three times: where a path reads the value after
//! it was given away, where it hands the value to a lambda that may not run,
//! and which value it drops, which is the one that the error is at.

use std::collections::BTreeSet;

use smith::check;
use smith::diag::{Error, Pos};

use crate::Test;
use crate::generate::{self, Gen};
use crate::programs::pos_after;

pub const ONCE: &[Test] = &[
    (
        "a_value_without_discard_is_refused_exactly_where_a_path_drops_it_or_takes_it_again",
        a_value_without_discard_is_refused_exactly_where_a_path_drops_it_or_takes_it_again,
    ),
    (
        "a_function_that_forgets_a_value_without_discard_is_one_error_at_the_value",
        a_function_that_forgets_a_value_without_discard_is_one_error_at_the_value,
    ),
    (
        "the_examples_of_values_used_exactly_once_do_what_the_page_says",
        the_examples_of_values_used_exactly_once_do_what_the_page_says,
    ),
    (
        "a_lambda_that_takes_a_value_without_discard_is_refused_where_it_may_not_run",
        a_lambda_that_takes_a_value_without_discard_is_refused_where_it_may_not_run,
    ),
    (
        "a_value_that_is_made_and_not_used_is_dropped",
        a_value_that_is_made_and_not_used_is_dropped,
    ),
];

/// The errors a program gives, which are none when it is accepted.
fn errors(source: &str) -> Vec<Error> {
    match check(source.as_bytes()) {
        Ok(_) => Vec::new(),
        Err(errors) => errors,
    }
}

/// `Res` has neither trait, and `Tok` has `Discard` and not `Copy`.
const TYPES: &str = "struct Res {
  n: u64
}

without Copy for Res
without Discard for Res

struct Tok {
  secret: u64
}

without Copy for Tok

struct Box[T] {
  item: T
}

enum Dir {
  north
  south
  east
}

fn look[T](x: T) -> u64 { 1 }

fn sink[T](x: take T) ! diverge {
  loop {
    n = 1
  }
}

fn gate(c: bool) -> Result[u64, u64] {
  if c { .ok(1) } else { .err(1) }
}

fn lent_fn[T](f: fn() -> T) -> T { f() }

fn take_fn[T](f: take fn() -> T) -> T { f() }

";

/// What a value holds.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Held {
    Res,
    Tok,
}

impl Held {
    fn make(self) -> &'static str {
        match self {
            Held::Res => "Res(n: 1)",
            Held::Tok => "Tok(secret: 1)",
        }
    }

    fn name(self) -> &'static str {
        match self {
            Held::Res => "Res",
            Held::Tok => "Tok",
        }
    }
}

/// How the value holds what it holds.
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

    fn ty(self, held: Held) -> String {
        let h = held.name();
        match self {
            Shape::Plain => h.to_string(),
            Shape::Struct => format!("Box[{h}]"),
            Shape::Tuple => format!("({h}, u64)"),
            Shape::Enum => format!("Option[{h}]"),
            Shape::List => format!("List[{h}]"),
        }
    }

    fn make(self, held: Held) -> String {
        let h = held.make();
        match self {
            Shape::Plain => h.to_string(),
            Shape::Struct => format!("Box(item: {h})"),
            Shape::Tuple => format!("({h}, 1)"),
            Shape::Enum => format!("Option.some({h})"),
            Shape::List => format!("[{h}]"),
        }
    }
}

/// Where the value comes from.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Origin {
    Param,
    Binding,
    Var,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Form {
    /// Lends it.
    Look,
    /// Lends a part of it, where it has a named one.
    Part,
    /// Gives it to a function that takes it.
    Eat,
    /// Takes an enum apart and takes what it holds.
    Split,
    /// Takes an enum apart and drops what it holds.
    Spill,
    /// Gives it away inside a lambda passed to a function that lends it.
    LamLent,
    /// The same, to a function that takes it.
    LamTake,
    /// Assigns the `var` a new value.
    Again,
    /// Leaves by `return`.
    Return,
    /// Leaves by a `try` that may stop.
    Try,
    /// Leaves the loop.
    Break,
}

#[derive(Clone, Debug)]
enum Item {
    Act(usize, Form),
    If(Vec<Item>, Option<Vec<Item>>),
    Arms(Vec<Vec<Item>>),
    Loop(Vec<Item>),
}

fn items(g: &mut Gen, depth: usize, in_loop: bool, next: &mut usize, origin: Origin) -> Vec<Item> {
    let mut forms = vec![
        Form::Look,
        Form::Part,
        Form::Eat,
        Form::Split,
        Form::Spill,
        Form::LamLent,
        Form::LamTake,
        Form::Return,
        Form::Try,
    ];
    if origin == Origin::Var {
        forms.push(Form::Again);
        forms.push(Form::Again);
    }
    if in_loop {
        forms.push(Form::Break);
    }
    let made: Vec<Item> = (0..1 + g.below(3))
        .map(|_| match if depth == 0 { 0 } else { g.below(5) } {
            1 => Item::If(
                items(g, depth - 1, in_loop, next, origin),
                g.one_in(2)
                    .then(|| items(g, depth - 1, in_loop, next, origin)),
            ),
            2 => Item::Arms(
                (0..3)
                    .map(|_| items(g, depth - 1, in_loop, next, origin))
                    .collect(),
            ),
            3 => Item::Loop(items(g, depth - 1, true, next, origin)),
            _ => {
                *next += 1;
                Item::Act(*next, *g.pick(&forms))
            }
        })
        .collect();
    // No line follows one that leaves, as a line that never runs is refused.
    let mut kept = Vec::new();
    for item in made {
        let leaves = leaves(&item);
        kept.push(item);
        if leaves {
            break;
        }
    }
    kept
}

/// Whether a statement leaves the block, by `return` or `break`, on every path.
fn leaves(item: &Item) -> bool {
    match item {
        Item::Act(_, f) => matches!(f, Form::Return | Form::Break),
        Item::If(then, Some(otherwise)) => ends(then) && ends(otherwise),
        Item::If(_, None) | Item::Loop(_) => false,
        Item::Arms(arms) => arms.iter().all(|a| ends(a)),
    }
}

fn ends(items: &[Item]) -> bool {
    items.last().is_some_and(leaves)
}

#[derive(Clone, Debug)]
struct Case {
    held: Held,
    shape: Shape,
    origin: Origin,
    items: Vec<Item>,
}

fn case(g: &mut Gen) -> Case {
    let held = *g.pick(&[Held::Res, Held::Tok]);
    let shape = *g.pick(&Shape::ALL);
    let origin = *g.pick(&[Origin::Param, Origin::Binding, Origin::Var]);
    let mut items = items(g, 2, false, &mut 0, origin);
    // Half of the functions end by taking the value, so that many are
    // accepted.
    if g.one_in(2) && !ends(&items) {
        items.push(Item::Act(1000, Form::Eat));
    }
    Case {
        held,
        shape,
        origin,
        items,
    }
}

/// The form a statement has for a shape: what a shape has no way to do is
/// done as another.
fn form(shape: Shape, form: Form) -> Form {
    match (form, shape) {
        (Form::Part, Shape::Plain | Shape::Tuple | Shape::Enum) => Form::Look,
        (Form::Split | Form::Spill, shape) if shape != Shape::Enum => Form::Eat,
        _ => form,
    }
}

/// One state of the value on a path: whether it is gone, and which value it
/// is, by the number of the place that made it.
type Live = (bool, usize);

/// What the paths of a body do.
#[derive(Default)]
struct Paths {
    /// The statements that use the value after it was given away.
    again: BTreeSet<usize>,
    /// The statements whose lambda may not run.
    lambdas: BTreeSet<usize>,
    /// The statements whose taken-apart value is dropped.
    spilled: BTreeSet<usize>,
    /// The values that a path drops: 0 is the first, and each other is the
    /// one a statement assigned.
    dropped: BTreeSet<usize>,
}

/// What a run of statements leaves: the states that go on, and those that
/// leave the loop.
struct Ends {
    next: BTreeSet<Live>,
    out: BTreeSet<Live>,
}

impl Paths {
    fn run(&mut self, items: &[Item], start: &BTreeSet<Live>, shape: Shape, lacks: bool) -> Ends {
        let mut now = start.clone();
        let mut out = BTreeSet::new();
        for item in items {
            if now.is_empty() {
                break;
            }
            let mut next = BTreeSet::new();
            match item {
                Item::Act(id, f) => {
                    let f = form(shape, *f);
                    // A statement that reads what a path gave away is
                    // reported as that, and a lambda that gives it away is
                    // reported only where no path did.
                    let any = now.iter().any(|(gone, _)| *gone);
                    if f == Form::Spill && lacks {
                        self.spilled.insert(*id);
                    }
                    let lambda = matches!(f, Form::LamLent | Form::LamTake);
                    if lambda && !any && (f == Form::LamLent || lacks) {
                        self.lambdas.insert(*id);
                    }
                    for &(gone, cur) in &now {
                        let mut live = Some((gone, cur));
                        match f {
                            Form::Look | Form::Part if gone => {
                                self.again.insert(*id);
                            }
                            Form::Look | Form::Part => {}
                            Form::Eat | Form::Split | Form::Spill | Form::LamLent | Form::LamTake
                                if gone =>
                            {
                                self.again.insert(*id);
                            }
                            Form::Eat | Form::Split | Form::Spill | Form::LamLent | Form::LamTake => {
                                live = Some((true, cur));
                            }
                            Form::Again => {
                                if !gone {
                                    self.dropped.insert(cur);
                                }
                                live = Some((false, *id));
                            }
                            Form::Return => {
                                if !gone {
                                    self.dropped.insert(cur);
                                }
                                live = None;
                            }
                            Form::Try => {
                                if !gone {
                                    self.dropped.insert(cur);
                                }
                            }
                            Form::Break => {
                                out.insert((gone, cur));
                                live = None;
                            }
                        }
                        next.extend(live);
                    }
                }
                Item::If(then, otherwise) => {
                    let a = self.run(then, &now, shape, lacks);
                    next.extend(a.next);
                    out.extend(a.out);
                    match otherwise {
                        Some(otherwise) => {
                            let b = self.run(otherwise, &now, shape, lacks);
                            next.extend(b.next);
                            out.extend(b.out);
                        }
                        None => next.extend(now.iter().copied()),
                    }
                }
                Item::Arms(arms) => {
                    for arm in arms {
                        let a = self.run(arm, &now, shape, lacks);
                        next.extend(a.next);
                        out.extend(a.out);
                    }
                }
                Item::Loop(body) => {
                    // A loop goes round any number of times, or none.
                    let mut round = now.clone();
                    next.extend(now.iter().copied());
                    for _ in 0..3 {
                        let a = self.run(body, &round, shape, lacks);
                        next.extend(a.next.iter().copied());
                        next.extend(a.out);
                        // The next round begins where the loop began or
                        // where a round ended.
                        round.extend(a.next);
                    }
                }
            }
            now = next;
        }
        Ends { next: now, out }
    }
}

/// The program, and where each place that may be refused is.
struct Written {
    source: String,
    /// Where the value first comes from.
    first: Pos,
    /// Where each statement reads the value.
    reads: Vec<(usize, Pos)>,
    /// Where the lambda of each statement is.
    lambdas: Vec<(usize, Pos)>,
    /// Where each statement names what it holds apart, or assigns.
    names: Vec<(usize, Pos)>,
}

fn pos_of(list: &[(usize, Pos)], id: usize) -> Pos {
    list.iter()
        .find(|(i, _)| *i == id)
        .map(|(_, p)| *p)
        .unwrap_or_else(|| panic!("no place for statement {id}"))
}

fn write_items(
    items: &[Item],
    c: &Case,
    out: &mut String,
    w: &mut Written,
    depth: usize,
    loops: &mut usize,
) {
    let indent = "  ".repeat(depth);
    for item in items {
        match item {
            Item::Act(id, f) => {
                let id = *id;
                let f = form(c.shape, *f);
                match f {
                    Form::Look | Form::Part | Form::Eat => {
                        let (call, value) = match (f, c.shape) {
                            (Form::Look, _) => ("look(", "a"),
                            (Form::Part, Shape::Struct) => ("look(", "a.item"),
                            (Form::Part, _) => ("look(", "a.size"),
                            _ => ("sink(", "a"),
                        };
                        out.push_str(&format!("{indent}n{id} = {call}"));
                        w.reads.push((id, pos_after(out)));
                        out.push_str(&format!("{value})\n"));
                    }
                    Form::Split | Form::Spill => {
                        out.push_str(&format!("{indent}match "));
                        w.reads.push((id, pos_after(out)));
                        out.push_str("a {\n");
                        out.push_str(&format!("{indent}  .some("));
                        w.names.push((id, pos_after(out)));
                        out.push_str(&format!("x{id}) {{\n"));
                        if f == Form::Split {
                            out.push_str(&format!("{indent}    sink(x{id})\n"));
                        } else {
                            out.push_str(&format!("{indent}    0\n"));
                        }
                        out.push_str(&format!(
                            "{indent}  }}\n{indent}  .none {{\n{indent}    0\n{indent}  }}\n{indent}}}\n"
                        ));
                    }
                    Form::LamLent | Form::LamTake => {
                        let callee = if f == Form::LamLent {
                            "lent_fn"
                        } else {
                            "take_fn"
                        };
                        out.push_str(&format!("{indent}n{id} = {callee}() "));
                        w.lambdas.push((id, pos_after(out)));
                        out.push_str("{ ");
                        w.reads.push((id, pos_after(out)));
                        out.push_str(&format!("a }}\n{indent}sink(n{id})\n"));
                    }
                    Form::Again => {
                        out.push_str(&indent);
                        w.names.push((id, pos_after(out)));
                        out.push_str("a = mk()\n");
                    }
                    Form::Return => out.push_str(&format!("{indent}return .ok(1)\n")),
                    Form::Try => out.push_str(&format!("{indent}n{id} = try gate(c)\n")),
                    Form::Break => out.push_str(&format!("{indent}break\n")),
                }
            }
            Item::If(then, otherwise) => {
                out.push_str(&format!("{indent}if c {{\n"));
                write_items(then, c, out, w, depth + 1, loops);
                if !ends(then) {
                    out.push_str(&format!("{indent}  0\n"));
                }
                match otherwise {
                    Some(otherwise) => {
                        out.push_str(&format!("{indent}}} else {{\n"));
                        write_items(otherwise, c, out, w, depth + 1, loops);
                        if !ends(otherwise) {
                            out.push_str(&format!("{indent}  0\n"));
                        }
                        out.push_str(&format!("{indent}}}\n"));
                    }
                    None => out.push_str(&format!("{indent}}}\n")),
                }
            }
            Item::Arms(arms) => {
                out.push_str(&format!("{indent}match d {{\n"));
                for (arm, case) in arms.iter().zip(["north", "south", "east"]) {
                    out.push_str(&format!("{indent}  .{case} {{\n"));
                    write_items(arm, c, out, w, depth + 2, loops);
                    if !ends(arm) {
                        out.push_str(&format!("{indent}    0\n"));
                    }
                    out.push_str(&format!("{indent}  }}\n"));
                }
                out.push_str(&format!("{indent}}}\n"));
            }
            Item::Loop(body) => {
                *loops += 1;
                out.push_str(&format!("{indent}loop i{} in 0..3 {{\n", *loops));
                write_items(body, c, out, w, depth + 1, loops);
                out.push_str(&format!("{indent}}}\n"));
            }
        }
    }
}

fn write(c: &Case) -> Written {
    let ty = c.shape.ty(c.held);
    let effect = if c.shape == Shape::List {
        " ! alloc, diverge"
    } else {
        " ! diverge"
    };
    let mut w = Written {
        source: String::new(),
        first: Pos { line: 0, col: 0 },
        reads: Vec::new(),
        lambdas: Vec::new(),
        names: Vec::new(),
    };
    let alloc = if c.shape == Shape::List { " ! alloc" } else { "" };
    let mut out = format!(
        "{TYPES}fn mk() -> {ty}{alloc} {{ {} }}\n\nfn f(",
        c.shape.make(c.held)
    );
    if c.origin == Origin::Param {
        w.first = pos_after(&out);
        out.push_str(&format!("a: take {ty}, "));
    }
    out.push_str(&format!(
        "c: bool, d: Dir) -> Result[u64, u64]{effect}\n{{\n"
    ));
    match c.origin {
        Origin::Param => {}
        Origin::Binding => {
            out.push_str("  ");
            w.first = pos_after(&out);
            out.push_str("a = mk()\n");
        }
        Origin::Var => {
            out.push_str("  var ");
            w.first = pos_after(&out);
            out.push_str("a = mk()\n");
        }
    }
    write_items(&c.items, c, &mut out, &mut w, 1, &mut 0);
    if !ends(&c.items) {
        out.push_str("  .ok(0)\n");
    }
    out.push_str(&format!("}}\n\nfn main(){effect} {{\n"));
    if c.origin == Origin::Param {
        out.push_str("  v = mk()\n  r = f(v, true, Dir.north)\n");
    } else {
        out.push_str("  r = f(true, Dir.north)\n");
    }
    out.push_str("  s = r\n}\n");
    w.source = out;
    w
}

fn a_value_without_discard_is_refused_exactly_where_a_path_drops_it_or_takes_it_again() {
    generate::check(4341, 100, case, |c| {
        let w = write(c);
        let found = errors(&w.source);
        let lacks = c.held == Held::Res;
        let mut paths = Paths::default();
        let start: BTreeSet<Live> = [(false, 0)].into();
        let end = paths.run(&c.items, &start, c.shape, lacks);
        if lacks {
            for (gone, cur) in end.next {
                if !gone {
                    paths.dropped.insert(cur);
                }
            }
        }
        let mut wanted: Vec<Pos> = Vec::new();
        wanted.extend(paths.again.iter().map(|id| pos_of(&w.reads, *id)));
        wanted.extend(paths.lambdas.iter().map(|id| pos_of(&w.lambdas, *id)));
        if lacks {
            wanted.extend(paths.spilled.iter().map(|id| pos_of(&w.names, *id)));
            wanted.extend(paths.dropped.iter().map(|id| match id {
                0 => w.first,
                id => pos_of(&w.names, *id),
            }));
        }
        wanted.sort();
        let got: Vec<Pos> = found.iter().map(|e| e.pos).collect();
        assert_eq!(got, wanted, "{found:?}\n{}", w.source);
        for e in &found {
            if paths.again.iter().any(|id| pos_of(&w.reads, *id) == e.pos) {
                assert!(e.message.contains("after it was given away"), "{e}");
            } else if paths.lambdas.iter().any(|id| pos_of(&w.lambdas, *id) == e.pos) {
                assert!(e.message.contains("this lambda"), "{e}");
            } else {
                assert!(e.message.contains("nothing takes it"), "{e}");
                assert!(e.message.contains("Res"), "{e}");
            }
        }
    });
}

// What the issue says, as it says it.

fn a_function_that_forgets_a_value_without_discard_is_one_error_at_the_value() {
    let head = "struct Res {
  n: u64
}

without Copy for Res
without Discard for Res

fn forget(";
    let source = format!("{head}r: take Res) -> Nat {{ 2 }}\n\nfn main() {{\n  n = forget(Res(n: 1))\n}}\n");
    let found = errors(&source);
    assert_eq!(found.len(), 1, "{found:?}");
    assert_eq!(found[0].pos, pos_after(head));
    assert!(found[0].message.contains("`r`"), "{}", found[0]);
    assert!(found[0].message.contains("`Res`"), "{}", found[0]);
    assert!(found[0].message.contains("nothing takes it"), "{}", found[0]);
    assert!(found[0].message.contains("cannot be dropped"), "{}", found[0]);
}

/// The types and the functions the examples of the page use.
const PAGE: &str = "struct Transaction {
  id: u64
}

without Copy for Transaction
without Discard for Transaction

struct Db {
  n: u64
}

struct DbError {
  n: u64
}

fn begin(db: Db) -> Transaction { Transaction(id: db.n) }

fn debit(t: Transaction, amount: u64) -> u64 { amount }

fn prepare(db: Db) -> Result[(), DbError] { .ok(()) }

fn end[T](x: take T) ! diverge {
  loop {
    n = 1
  }
}

fn commit(t: take Transaction) -> Result[(), DbError] ! diverge {
  end(t)
  .ok(())
}

fn rollback(t: take Transaction) -> Result[(), DbError] ! diverge {
  end(t)
  .ok(())
}

";

fn the_examples_of_values_used_exactly_once_do_what_the_page_says() {
    let program = |body: &str| format!("{PAGE}{body}\n\nfn main() ! diverge {{\n  r = run(Db(n: 1), true)\n}}\n");
    // Each accepted example, and each refused one with where it is refused.
    let accepted = [
        "fn run(db: Db, ok: bool) -> Result[(), DbError] ! diverge {
  t = begin(db)
  debit(t, 100)
  commit(t)
}",
        "fn run(db: Db, ok: bool) -> Result[(), DbError] ! diverge {
  t = begin(db)
  if ok { commit(t) } else { rollback(t) }
}",
    ];
    for body in accepted {
        let source = program(body);
        assert!(errors(&source).is_empty(), "{:?}\n{source}", errors(&source));
    }
    let refused = [
        (
            "fn run(db: Db, ok: bool) -> Result[(), DbError] ! diverge {
  t = begin(db)
  debit(t, 100)
  .ok(())
}",
            "t = begin",
            "nothing takes it",
        ),
        (
            "fn run(db: Db, ok: bool) -> Result[(), DbError] ! diverge {
  t = begin(db)
  try commit(t)
  commit(t)
}",
            "commit(t)\n}",
            "after it was given away",
        ),
        (
            "fn run(db: Db, ok: bool) -> Result[(), DbError] ! diverge {
  t = begin(db)
  if ok { commit(t) } else { .ok(()) }
}",
            "t = begin",
            "nothing takes it",
        ),
        (
            "fn run(db: Db, ok: bool) -> Result[(), DbError] ! diverge {
  t = begin(db)
  try prepare(db)
  commit(t)
}",
            "t = begin",
            "nothing takes it",
        ),
        (
            "fn run(db: Db, ok: bool) -> Result[(), DbError] ! diverge {
  var t = begin(db)
  debit(t, 100)
  t = begin(db)
  commit(t)
}",
            "t = begin",
            "nothing takes it",
        ),
    ];
    for (body, at, says) in refused {
        let source = program(body);
        let found = errors(&source);
        assert_eq!(found.len(), 1, "{found:?}\n{source}");
        let mut place = source.find(at).unwrap();
        if at.starts_with("commit") {
            // The second use of `t`.
            place += "commit(".len();
        }
        assert_eq!(found[0].pos, pos_after(&source[..place]), "{source}");
        assert!(found[0].message.contains(says), "{}", found[0]);
    }
}

fn a_lambda_that_takes_a_value_without_discard_is_refused_where_it_may_not_run() {
    for (takes, errors_at_lambda) in [("take ", true), ("", true)] {
        let head = format!(
            "{TYPES}fn maybe[T](c: bool, f: {takes}fn() -> T) -> u64 {{ 1 }}

fn run(r: take Res) -> u64 ! diverge {{
  maybe(true) "
        );
        let source = format!("{head}{{ r }}\n}}\n\nfn main() ! diverge {{\n  n = run(Res(n: 1))\n}}\n");
        let found = errors(&source);
        assert_eq!(found.len() == 1, errors_at_lambda, "{found:?}\n{source}");
        assert_eq!(found[0].pos, pos_after(&head), "{source}");
        assert!(found[0].message.contains("`r`"), "{}", found[0]);
    }
}

fn a_value_that_is_made_and_not_used_is_dropped() {
    let head = format!("{TYPES}fn make() -> Res {{ Res(n: 1) }}\n\nfn run() -> u64 {{\n  ");
    let source = format!("{head}make()\n  1\n}}\n\nfn main() {{\n  n = run()\n}}\n");
    let found = errors(&source);
    assert_eq!(found.len(), 1, "{found:?}\n{source}");
    assert_eq!(found[0].pos, pos_after(&head));
    assert!(found[0].message.contains("nothing takes it"), "{}", found[0]);
}
