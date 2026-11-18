//! The generator of programs that pass the rest of a block to a function
//! with `with`, and continue only on success with `try`, which can also
//! place one mistake in what it builds.
//!
//! A program has the helpers `find`, which turns an `Option` into a
//! `Result`, `via`, which takes a lambda that gives a `Result`, and `wrap`,
//! `each`, and `tagged`, which take the rest of a block. It also has an
//! enum `Maybe` and an enum `Done`, with an implementation of `Try` of the
//! program's own. Its functions give a `Result`, an `Option`, or a `Done`,
//! and each is a list of steps that continue only on success: a step
//! continues with what a call of `find`, of an earlier function, of `via`
//! with a lambda that holds a step of its own, or of a `match` gives, or
//! with an `Option` or a `Maybe` it is given. `main` calls them, and then
//! passes the rest of its block to the helpers that take one.
//!
//! The generator first builds a model of the program and works out from it
//! what the program prints. Then it writes the model out three ways that
//! mean the same: with `try` and `with`, as written, with every `try` as the
//! `match` on what `Try.split` gives that it means, and with every `with` as
//! the call and the lambda after it that it means. A mistake is placed as the
//! model is written the first way.

use crate::generate::Gen;
use crate::programs::{Expected, pos_after};

/// A program that passes blocks on and leaves on a failure: its source in
/// each of its three spellings, what it prints, and the mistake placed in
/// it, if any.
pub struct TryCase {
    pub source: String,
    /// The same program with every `try` written as the `match` it means.
    pub matched: String,
    /// The same program with every `with` written as the call and the lambda
    /// after it that it means.
    pub lambdas: String,
    pub stdout: Vec<u8>,
    pub mistake: Option<Expected>,
}

impl std::fmt::Debug for TryCase {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{:?}", self.source)
    }
}

/// A value that may be missing: an `Option[str]`, or a `Maybe`.
type Val = Option<String>;

/// The family of types a function works with.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Fam {
    /// Gives a `Result[str, str]`.
    R,
    /// Gives an `Option[str]`.
    O,
    /// Gives a `Done`, from `Maybe`s.
    M,
}

/// An argument: a parameter of the function, the parameter of the lambda
/// `via` takes, or a value written out.
#[derive(Clone, Debug)]
enum Arg {
    A,
    B,
    V,
    Lit(Val),
}

impl Arg {
    fn text(&self, fam: Fam) -> String {
        match (self, fam) {
            (Arg::A, _) => "a".into(),
            (Arg::B, _) => "b".into(),
            (Arg::V, _) => "v".into(),
            (Arg::Lit(Some(s)), Fam::M) => format!(".yes(\"{s}\")"),
            (Arg::Lit(None), Fam::M) => ".no".into(),
            (Arg::Lit(Some(s)), _) => format!(".some(\"{s}\")"),
            (Arg::Lit(None), _) => ".none".into(),
        }
    }

    fn value(&self, a: &Val, b: &Val) -> Val {
        match self {
            Arg::A => a.clone(),
            Arg::B => b.clone(),
            Arg::V => unreachable!("only inside a lambda"),
            Arg::Lit(v) => v.clone(),
        }
    }
}

/// What a step continues with.
#[derive(Clone, Debug)]
enum Op {
    /// `find(name, arg)`.
    Find(String, Arg),
    /// A call of an earlier function that gives a `Result`.
    Call(usize, Arg, Arg),
    /// `via(arg) fn(v) { y = try find(name, v) ... }`.
    Via(String, Arg),
    /// A `match` on `arg` whose blocks give a `Result`.
    Match(String, Arg),
    /// A parameter, which is an `Option` or a `Maybe`.
    Param(Arg),
    /// A call of an earlier function that gives an `Option`.
    CallO(usize, Arg, Arg),
    /// Three parts, each of which may print, fail, or give a text, written
    /// in one expression of a shape. They run in the order they are
    /// written.
    Order(Shape, [Part; 3]),
}

/// An expression with parts that run one after another.
#[derive(Clone, Copy, Debug)]
enum Shape {
    /// `join3(a, b, c)`.
    Call,
    /// `join_t((a, b, c))`.
    Tuple,
    /// `join_s(Trio(first: a, second: b, third: c))`.
    Struct,
    /// `"{a}{b}{c}"`.
    Text,
    /// `join_o(.some(a), b, c)`.
    Opt,
    /// `join_n((a, b), c)`.
    Pair,
}

#[derive(Clone, Debug)]
enum Part {
    /// `say(label)`, which prints the label and gives it.
    Say(String),
    /// `try find(name, arg)`.
    Try(String, Arg),
    /// A text.
    Lit(String),
    /// What an earlier step bound, by the place of the step.
    Name(usize),
    /// `"<{say(label)}>"`, a string that holds an expression.
    Interp(String),
    /// A `match` whose blocks each print a label.
    Match(String, String, Arg),
    /// `app { say(label) }`, a call that takes a lambda.
    Lam(String),
    /// A field of a construction that prints.
    Fld(String),
}

#[derive(Clone, Debug)]
struct Step {
    op: Op,
    /// Whether what it continues with is bound to a name, so that the step
    /// is `x = try e` and not `try e`.
    bind: bool,
}

/// How a function ends.
#[derive(Clone, Debug)]
enum Final {
    /// Gives the names bound, one after another, as a success.
    Concat,
    /// Gives what `find` gives.
    Tail(String, Arg),
}

#[derive(Clone, Debug)]
struct Func {
    steps: Vec<Step>,
    fin: Final,
}

/// A call from `main`.
#[derive(Clone, Debug)]
struct Call {
    fam: Fam,
    f: usize,
    a: Val,
    b: Val,
}

/// An item of what `main` passes its block to, in order.
#[derive(Clone, Debug)]
enum Item {
    Print(String),
    /// Prints what is bound, which is the name of an item before.
    PrintBound(usize),
    Wrap(String),
    /// Binds a value to the name `e{index}`.
    Each(String),
    Tagged(Option<String>),
}

/// The kinds of mistake the generator can place.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Kind {
    /// A statement that gives a `Result` and does nothing with it.
    Dropped,
    /// `try` on a type with no `Try`.
    NoTry,
    /// `try` in a function whose result cannot hold the failure.
    CannotHold,
    /// `try` in `main`, which gives nothing.
    InMain,
    /// `with` on a call whose last parameter takes no function.
    NoFunction,
    /// `with x =` where the function takes no parameter.
    TooMany,
    /// `with` with no name where the function takes a parameter.
    TooFew,
}

const KINDS: &[Kind] = &[
    Kind::Dropped,
    Kind::NoTry,
    Kind::CannotHold,
    Kind::InMain,
    Kind::NoFunction,
    Kind::TooMany,
    Kind::TooFew,
];

struct Model {
    r: Vec<Func>,
    o: Vec<Func>,
    m: Func,
    calls: Vec<Call>,
    items: Vec<Item>,
    /// The mistake: its kind, and for the first three the step of the
    /// first function that gives a `Result` that it goes before.
    mistake: Option<(Kind, usize)>,
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Style {
    /// As written: `try` where the generator chose it, and `with`.
    Sugar,
    /// Every `try` as the `match` on `Try.split` it means.
    Match,
    /// Every `with` as a call and the lambda after it.
    Lambdas,
}

pub fn program(g: &mut Gen) -> TryCase {
    build(g, None)
}

/// A program with one mistake in it.
pub fn program_with_mistake(g: &mut Gen) -> TryCase {
    let kind = *g.pick(KINDS);
    build(g, Some(kind))
}

fn lit(g: &mut Gen, k: &mut usize) -> Val {
    if g.one_in(3) {
        None
    } else {
        *k += 1;
        Some(format!("t{k}"))
    }
}

fn arg(g: &mut Gen, k: &mut usize, inside: bool) -> Arg {
    match g.below(if inside { 3 } else { 4 }) {
        0 => Arg::A,
        1 => Arg::B,
        2 => Arg::Lit(lit(g, k)),
        _ => Arg::V,
    }
}

fn name(k: &mut usize) -> String {
    *k += 1;
    format!("n{k}")
}

fn func(g: &mut Gen, fam: Fam, earlier_r: usize, earlier_o: usize, k: &mut usize) -> Func {
    let count = g.below(4) + usize::from(fam == Fam::M);
    let mut steps = Vec::new();
    for _ in 0..count {
        let options = match fam {
            Fam::R if earlier_r > 0 => 4,
            Fam::R => 3,
            Fam::O if earlier_o > 0 => 2,
            _ => 1,
        };
        let mut pick = g.below(options);
        if fam == Fam::R && earlier_r == 0 && pick == 3 {
            pick = 0;
        }
        let order = fam == Fam::R && g.one_in(3);
        let op = match (fam, pick) {
            (Fam::R, _) if order => {
                let shape = *g.pick(&[
                    Shape::Call,
                    Shape::Tuple,
                    Shape::Struct,
                    Shape::Text,
                    Shape::Opt,
                    Shape::Pair,
                ]);
                let earlier: Vec<usize> = (steps.iter().enumerate())
                    .filter(|(_, s): &(usize, &Step)| s.bind)
                    .map(|(i, _)| i)
                    .collect();
                let part = |g: &mut Gen, k: &mut usize| {
                    // A string holds only what is written in a line.
                    let kinds = if matches!(shape, Shape::Text) { 4 } else { 8 };
                    match g.below(kinds) {
                        0 => Part::Say(name(k)),
                        1 => Part::Try(name(k), arg(g, k, false).no_v()),
                        2 => Part::Lit(name(k)),
                        3 if !earlier.is_empty() => Part::Name(*g.pick(&earlier)),
                        3 => Part::Lit(name(k)),
                        4 => Part::Interp(name(k)),
                        5 => Part::Match(name(k), name(k), arg(g, k, false).named()),
                        6 => Part::Lam(name(k)),
                        _ => Part::Fld(name(k)),
                    }
                };
                let parts = [part(g, k), part(g, k), part(g, k)];
                Op::Order(shape, parts)
            }
            (Fam::R, 0) => Op::Find(name(k), arg(g, k, false).no_v()),
            (Fam::R, 1) => Op::Via(name(k), arg(g, k, false).no_v()),
            (Fam::R, 2) => Op::Match(name(k), arg(g, k, false).named()),
            (Fam::R, _) => Op::Call(
                g.below(earlier_r),
                arg(g, k, false).no_v(),
                arg(g, k, false).no_v(),
            ),
            (Fam::O, 1) => Op::CallO(
                g.below(earlier_o),
                arg(g, k, false).no_v(),
                arg(g, k, false).no_v(),
            ),
            (_, _) => Op::Param(if g.one_in(2) { Arg::A } else { Arg::B }),
        };
        let bind = g.below(4) != 0 || matches!(op, Op::Order(..));
        steps.push(Step { op, bind });
    }
    let fin = if fam == Fam::R && g.one_in(3) {
        Final::Tail(name(k), arg(g, k, false).no_v())
    } else {
        Final::Concat
    };
    Func { steps, fin }
}

impl Arg {
    /// A parameter of the function, which a `match` can take apart.
    fn named(self) -> Arg {
        match self {
            Arg::V | Arg::Lit(_) => Arg::A,
            arg => arg,
        }
    }

    /// An argument that is not the parameter of a lambda.
    fn no_v(self) -> Arg {
        match self {
            Arg::V => Arg::A,
            arg => arg,
        }
    }
}

fn model(g: &mut Gen, kind: Option<Kind>) -> Model {
    let mut k = 0;
    let n_r = g.below(3) + 1;
    let n_o = g.below(3);
    let mut r: Vec<Func> = Vec::new();
    let mut o: Vec<Func> = Vec::new();
    for i in 0..n_r {
        let f = func(g, Fam::R, i, 0, &mut k);
        r.push(f);
    }
    for i in 0..n_o {
        let f = func(g, Fam::O, 0, i, &mut k);
        o.push(f);
    }
    let m = func(g, Fam::M, 0, 0, &mut k);
    let mut calls = Vec::new();
    for (fam, n) in [(Fam::R, n_r), (Fam::O, n_o), (Fam::M, 1)] {
        for f in 0..n {
            for _ in 0..=g.below(2) {
                let a = lit(g, &mut k);
                let b = lit(g, &mut k);
                calls.push(Call { fam, f, a, b });
            }
        }
    }
    let mut items = Vec::new();
    let mut bound = 0;
    for _ in 0..g.below(5) {
        items.push(match g.below(5) {
            0 => Item::Print(format!("p{}", items.len())),
            1 if bound > 0 => Item::PrintBound(g.below(bound)),
            2 => Item::Wrap(format!("w{}", items.len())),
            3 => {
                bound += 1;
                Item::Each(format!("e{}", items.len()))
            }
            _ => Item::Tagged(g.one_in(2).then(|| format!("g{}", items.len()))),
        });
    }
    // `PrintBound` names an `Each` by its place among them.
    let mistake = kind.map(|kind| (kind, g.below(r[0].steps.len() + 1)));
    Model {
        r,
        o,
        m,
        calls,
        items,
        mistake,
    }
}

// What the program does, as the model says.

fn find(name: &str, have: &Val) -> Result<String, String> {
    match have {
        Some(s) => Ok(s.clone()),
        None => Err(format!("no {name}")),
    }
}

fn run_func(
    m: &Model,
    fam: Fam,
    f: usize,
    a: &Val,
    b: &Val,
    out: &mut String,
) -> Result<String, String> {
    let func = match fam {
        Fam::R => &m.r[f],
        Fam::O => &m.o[f],
        Fam::M => &m.m,
    };
    let mut bound = Vec::new();
    let mut values: Vec<String> = Vec::new();
    for step in &func.steps {
        let x = match &step.op {
            Op::Find(n, o) | Op::Via(n, o) => find(n, &o.value(a, b)),
            Op::Call(j, x, y) => run_func(m, Fam::R, *j, &x.value(a, b), &y.value(a, b), out),
            Op::Match(n, o) => match o.value(a, b) {
                Some(s) => find(n, &Some(s)),
                None => Err(format!("alt {n}")),
            },
            Op::Param(o) => o.value(a, b).ok_or_default(),
            Op::CallO(j, x, y) => run_func(m, Fam::O, *j, &x.value(a, b), &y.value(a, b), out),
            Op::Order(_, parts) => {
                let mut text = String::new();
                for part in parts {
                    let said = |label: &str, out: &mut String| out.push_str(&format!("{label}\n"));
                    match part {
                        Part::Say(label) | Part::Lam(label) | Part::Fld(label) => {
                            said(label, out);
                            text.push_str(label);
                        }
                        Part::Interp(label) => {
                            said(label, out);
                            text.push_str(&format!("<{label}>"));
                        }
                        Part::Match(first, second, o) => {
                            let label = if o.value(a, b).is_some() {
                                first
                            } else {
                                second
                            };
                            said(label, out);
                            text.push_str(label);
                        }
                        Part::Try(n, o) => text.push_str(&find(n, &o.value(a, b))?),
                        Part::Lit(lit) => text.push_str(lit),
                        Part::Name(j) => text.push_str(&values[*j]),
                    }
                }
                Ok(text)
            }
        }?;
        values.push(x.clone());
        if step.bind {
            bound.push(x);
        }
    }
    match &func.fin {
        Final::Concat => Ok(if bound.is_empty() {
            "z".to_string()
        } else {
            bound.concat()
        }),
        Final::Tail(n, o) => find(n, &o.value(a, b)),
    }
}

trait OkOrDefault {
    fn ok_or_default(self) -> Result<String, String>;
}

impl OkOrDefault for Val {
    fn ok_or_default(self) -> Result<String, String> {
        self.ok_or_else(String::new)
    }
}

fn shown(fam: Fam, r: Result<String, String>) -> String {
    match (fam, r) {
        (Fam::R, Ok(s)) => format!("ok {s}"),
        (Fam::R, Err(s)) => format!("err {s}"),
        (Fam::O, Ok(s)) => format!("some {s}"),
        (Fam::O, Err(_)) => "none".to_string(),
        (Fam::M, Ok(s)) => format!("value {s}"),
        (Fam::M, Err(_)) => "stop".to_string(),
    }
}

fn items_out(items: &[Item], at: usize, bound: &mut Vec<String>, out: &mut String) {
    let Some(item) = items.get(at) else {
        out.push_str("end\n");
        return;
    };
    match item {
        Item::Print(text) => {
            out.push_str(&format!("{text}\n"));
            items_out(items, at + 1, bound, out);
        }
        Item::PrintBound(i) => {
            out.push_str(&format!("{}!\n", bound[*i]));
            items_out(items, at + 1, bound, out);
        }
        Item::Wrap(label) => {
            out.push_str(&format!("<{label}\n"));
            items_out(items, at + 1, bound, out);
            out.push_str(&format!("{label}>\n"));
        }
        Item::Each(value) => {
            bound.push(value.clone());
            items_out(items, at + 1, bound, out);
            bound.pop();
        }
        Item::Tagged(tag) => {
            out.push_str(&format!("[{}]\n", tag.as_deref().unwrap_or("t")));
            items_out(items, at + 1, bound, out);
        }
    }
}

fn stdout(m: &Model) -> Vec<u8> {
    let mut out = String::new();
    for c in &m.calls {
        let r = run_func(m, c.fam, c.f, &c.a, &c.b, &mut out);
        out.push_str(&shown(c.fam, r));
        out.push('\n');
    }
    items_out(&m.items, 0, &mut Vec::new(), &mut out);
    out.into_bytes()
}

// How the program is written.

const HELPERS: &str = "\
enum Maybe {
  yes: str
  no
}

enum Done {
  value: str
  stop
}

impl Try[Maybe, Done, str] {
  fn split(c: take Maybe) -> Flow[str, Done] {
    match c {
      .yes(s) { .go(s) }
      .no { .stop(.stop) }
    }
  }
}

fn split_r(c: take Result[str, str]) -> Flow[str, Result[str, str]] { Try.split(c) }

fn split_o(c: take Option[str]) -> Flow[str, Option[str]] { Try.split(c) }

fn split_m(c: take Maybe) -> Flow[str, Done] { Try.split(c) }

fn find(name: str, have: Option[str]) -> Result[str, str] ! alloc {
  match have {
    .some(value) { .ok(value) }
    .none { .err(\"no {name}\") }
  }
}

fn via(have: Option[str], body: fn(Option[str]) -> Result[str, str]) -> Result[str, str] {
  body(have)
}

fn plain(s: str) -> str { s }

struct Trio {
  first: str
  second: str
  third: str
}

fn say(s: str) -> str ! console {
  print(s)
  s
}

fn join3(a: str, b: str, c: str) -> str ! alloc { \"{a}{b}{c}\" }

fn join_t(t: (str, str, str)) -> str ! alloc {
  (a, b, c) = t
  \"{a}{b}{c}\"
}

fn join_o(a: Option[str], b: str, c: str) -> str ! alloc {
  match a {
    .some(v) { \"{v}{b}{c}\" }
    .none { \"{b}{c}\" }
  }
}

fn join_n(t: (str, str), c: str) -> str ! alloc {
  (a, b) = t
  \"{a}{b}{c}\"
}

fn app(body: fn() -> str) -> str { body() }

fn join_s(t: Trio) -> str ! alloc { \"{t.first}{t.second}{t.third}\" }

fn wrap(label: str, body: fn()) ! console, alloc {
  print(\"<{label}\")
  body()
  print(\"{label}>\")
}

fn each(value: str, body: fn(str)) {
  body(value)
}

fn tagged(tag: str = \"t\", body: fn()) ! console, alloc {
  print(\"[{tag}]\")
  body()
}

fn show(r: Result[str, str]) ! console, alloc {
  match r {
    .ok(text) { print(\"ok {text}\") }
    .err(why) { print(\"err {why}\") }
  }
}

fn show_o(o: Option[str]) ! console, alloc {
  match o {
    .some(text) { print(\"some {text}\") }
    .none { print(\"none\") }
  }
}

fn show_m(d: Done) ! console, alloc {
  match d {
    .value(text) { print(\"value {text}\") }
    .stop { print(\"stop\") }
  }
}
";

struct Writer {
    text: String,
    style: Style,
    mistake: Option<Expected>,
}

impl Writer {
    fn indent(&mut self, by: usize) {
        for _ in 0..by {
            self.text.push(' ');
        }
    }

    fn line(&mut self, by: usize, text: &str) {
        self.indent(by);
        self.text.push_str(text);
        self.text.push('\n');
    }

    /// Writes the start of a line and records where the mistake is, which is
    /// where the rest of the line starts.
    fn mark(&mut self, mentions: &str, fix: Option<&str>) {
        assert!(self.mistake.is_none(), "one mistake");
        self.mistake = Some(Expected {
            at: pos_after(&self.text),
            mentions: mentions.to_string(),
            fix: fix.map(str::to_string),
        });
    }

    fn operand(&mut self, fam: Fam, op: &Op, by: usize) -> String {
        match op {
            Op::Find(n, o) => format!("find(\"{n}\", {})", o.text(fam)),
            Op::Call(j, x, y) => format!("r{j}({}, {})", x.text(Fam::R), y.text(Fam::R)),
            Op::CallO(j, x, y) => format!("o{j}({}, {})", x.text(Fam::O), y.text(Fam::O)),
            Op::Param(o) => o.text(fam),
            Op::Order(..) => unreachable!("written as a statement"),
            Op::Match(n, o) => {
                let pad = " ".repeat(by);
                format!(
                    "match {} {{\n{pad}  .some(v) {{ find(\"{n}\", .some(v)) }}\n\
                     {pad}  .none {{ .err(\"alt {n}\") }}\n{pad}}}",
                    o.text(Fam::R)
                )
            }
            Op::Via(n, o) => {
                // The lambda holds a step of its own, written in the style
                // of the program.
                let mut inner = Writer {
                    text: String::new(),
                    style: self.style,
                    mistake: None,
                };
                let step = Step {
                    op: Op::Find(n.clone(), Arg::V),
                    bind: true,
                };
                let f = Func {
                    steps: vec![step],
                    fin: Final::Concat,
                };
                inner.steps(Fam::R, &f, 0, by + 2, 1000, None);
                format!(
                    "via({}) fn(v) {{\n{}{}}}",
                    o.text(Fam::R),
                    inner.text,
                    " ".repeat(by)
                )
            }
        }
    }

    /// Writes the steps of `f` from step `i`, and how it ends. The names it
    /// binds are numbered from `base`.
    fn steps(
        &mut self,
        fam: Fam,
        f: &Func,
        i: usize,
        by: usize,
        base: usize,
        bad: Option<(Kind, usize)>,
    ) {
        if let Some((kind, at)) = bad
            && at == i
        {
            self.bad(kind, by);
        }
        let Some(step) = f.steps.get(i) else {
            return self.end(fam, f, by, base);
        };
        let x = format!("x{}", base + i);
        if let Op::Order(shape, parts) = &step.op {
            self.order(*shape, parts, &x, by, base);
            self.steps(fam, f, i + 1, by, base, bad);
            return;
        }
        let operand = self.operand(fam, &step.op, by);
        let split = self.split(fam, &operand, &x, by);
        if step.bind {
            self.line(by, &format!("{x} = {split}"));
        } else {
            self.line(by, &split);
        }
        self.steps(fam, f, i + 1, by, base, bad);
    }

    /// `try operand`, or the `match` that it means, where the names that the
    /// `match` binds start with `tag`. What `operand` splits into is given
    /// by the function of its family that splits it and says what the
    /// function that is written in gives, which `try` takes from the function.
    fn split(&self, fam: Fam, operand: &str, tag: &str, by: usize) -> String {
        if self.style != Style::Match {
            return format!("try {operand}");
        }
        let split = match fam {
            Fam::R => "split_r",
            Fam::O => "split_o",
            Fam::M => "split_m",
        };
        let pad = " ".repeat(by);
        format!(
            "match {split}({operand}) {{\n{pad}  .go({tag}g) {{ {tag}g }}\n\
             {pad}  .stop({tag}s) {{ return {tag}s }}\n{pad}}}"
        )
    }

    /// Writes `x = <the parts in a shape>`, where the parts that fail are
    /// `try`s, or the `match`es they mean, inside it, and run where they are
    /// written, from left to right.
    fn order(&mut self, shape: Shape, parts: &[Part; 3], x: &str, by: usize, base: usize) {
        let pad = " ".repeat(by);
        let texts: Vec<String> = (parts.iter().enumerate())
            .map(|(k, part)| match part {
                Part::Say(label) => format!("say(\"{label}\")"),
                Part::Try(n, o) => {
                    let find = format!("find(\"{n}\", {})", o.text(Fam::R));
                    self.split(Fam::R, &find, &format!("{x}p{k}"), by)
                }
                Part::Lit(lit) => format!("\"{lit}\""),
                Part::Name(j) => format!("x{}", base + j),
                Part::Interp(label) => format!("\"<{{say(\"{label}\")}}>\""),
                Part::Match(first, second, o) => format!(
                    "match {} {{\n{pad}  .some(v) {{ say(\"{first}\") }}\n\
                     {pad}  .none {{ say(\"{second}\") }}\n{pad}}}",
                    o.text(Fam::R)
                ),
                Part::Lam(label) => format!("app {{ say(\"{label}\") }}"),
                Part::Fld(label) => {
                    format!("Trio(first: say(\"{label}\"), second: \"s\", third: \"t\").first")
                }
            })
            .collect();
        // A `match` is not written inside the braces of a string, so the
        // parts of a text are bound first, in the order they run.
        let texts: Vec<String> = if self.style == Style::Match && matches!(shape, Shape::Text) {
            let mut bound = Vec::new();
            for (k, (part, text)) in parts.iter().zip(texts).enumerate() {
                if matches!(part, Part::Lit(_) | Part::Name(_)) {
                    bound.push(text);
                    continue;
                }
                let name = format!("{x}b{k}");
                self.line(by, &format!("{name} = {text}"));
                bound.push(name);
            }
            bound
        } else {
            texts
        };
        let text = match shape {
            Shape::Call => format!("join3({}, {}, {})", texts[0], texts[1], texts[2]),
            Shape::Tuple => format!("join_t(({}, {}, {}))", texts[0], texts[1], texts[2]),
            Shape::Struct => format!(
                "join_s(Trio(first: {}, second: {}, third: {}))",
                texts[0], texts[1], texts[2]
            ),
            Shape::Opt => format!("join_o(.some({}), {}, {})", texts[0], texts[1], texts[2]),
            Shape::Pair => format!("join_n(({}, {}), {})", texts[0], texts[1], texts[2]),
            Shape::Text => {
                let mut text = String::from("\"");
                for (part, t) in parts.iter().zip(&texts) {
                    match part {
                        Part::Lit(lit) => text.push_str(lit),
                        _ => text.push_str(&format!("{{{t}}}")),
                    }
                }
                text.push('"');
                text
            }
        };
        self.line(by, &format!("{x} = {text}"));
    }

    fn end(&mut self, fam: Fam, f: &Func, by: usize, base: usize) {
        let names: Vec<String> = (f.steps.iter().enumerate())
            .filter(|(_, s)| s.bind)
            .map(|(i, _)| format!("{{x{}}}", base + i))
            .collect();
        let text = if names.is_empty() {
            "z".to_string()
        } else {
            names.concat()
        };
        let ok = match fam {
            Fam::R => ".ok",
            Fam::O => ".some",
            Fam::M => ".value",
        };
        match &f.fin {
            Final::Concat => self.line(by, &format!("{ok}(\"{text}\")")),
            Final::Tail(n, o) => self.line(by, &format!("find(\"{n}\", {})", o.text(Fam::R))),
        }
    }

    /// The statement that holds the mistake.
    fn bad(&mut self, kind: Kind, by: usize) {
        self.indent(by);
        match kind {
            Kind::Dropped => {
                self.mark("nothing uses", Some("try"));
                self.text.push_str("find(\"u\", a)\n");
            }
            Kind::NoTry => {
                self.text.push_str("bad = ");
                self.mark("Try", None);
                self.text.push_str("try \"t\"\n");
            }
            Kind::CannotHold => {
                self.text.push_str("bad = ");
                self.mark("Try", None);
                self.text.push_str("try a\n");
            }
            _ => unreachable!("placed in `main`"),
        }
    }
}

fn function(w: &mut Writer, fam: Fam, f: &Func, i: usize, bad: Option<(Kind, usize)>) {
    let (name, ty, param) = match fam {
        Fam::R => (format!("r{i}"), "Result[str, str]", "Option[str]"),
        Fam::O => (format!("o{i}"), "Option[str]", "Option[str]"),
        Fam::M => ("run".to_string(), "Done", "Maybe"),
    };
    w.text.push_str(&format!(
        "fn {name}(a: {param}, b: {param}) -> {ty} ! console, alloc {{\n"
    ));
    w.steps(fam, f, 0, 2, 0, bad);
    w.text.push_str("}\n\n");
}

/// What `main` does after its calls: the items, each passing the rest of the
/// block to a function.
fn items(w: &mut Writer, m: &Model, at: usize, by: usize, bound: &mut Vec<String>) {
    let Some(item) = m.items.get(at) else {
        // The mistake in `main` comes last.
        if let Some((kind, _)) = m.mistake
            && !placed_in_function(kind)
        {
            main_mistake(w, kind, by);
        }
        w.line(by, "print(\"end\")");
        return;
    };
    let lambdas = w.style == Style::Lambdas;
    let open = |w: &mut Writer, text: &str, bound: &mut Vec<String>| {
        if lambdas {
            w.line(by, &format!("{text} {{"));
            items(w, m, at + 1, by + 2, bound);
            w.line(by, "}");
        } else {
            w.line(by, &format!("with {text}"));
            items(w, m, at + 1, by, bound);
        }
    };
    match item {
        Item::Print(text) => {
            w.line(by, &format!("print(\"{text}\")"));
            items(w, m, at + 1, by, bound);
        }
        Item::PrintBound(i) => {
            w.line(by, &format!("print(\"{{{}}}!\")", bound[*i]));
            items(w, m, at + 1, by, bound);
        }
        Item::Wrap(label) => open(w, &format!("wrap(\"{label}\")"), bound),
        Item::Tagged(None) => open(w, "tagged()", bound),
        Item::Tagged(Some(tag)) => open(w, &format!("tagged(\"{tag}\")"), bound),
        Item::Each(value) => {
            let name = format!("e{at}");
            bound.push(name.clone());
            if lambdas {
                w.line(by, &format!("each(\"{value}\") fn({name}) {{"));
                items(w, m, at + 1, by + 2, bound);
                w.line(by, "}");
            } else {
                w.line(by, &format!("with {name} = each(\"{value}\")"));
                items(w, m, at + 1, by, bound);
            }
            bound.pop();
        }
    }
}

fn main_mistake(w: &mut Writer, kind: Kind, by: usize) {
    w.indent(by);
    match kind {
        Kind::InMain => {
            w.text.push_str("bad = ");
            w.mark("Try", None);
            w.text.push_str("try find(\"m\", .none)\n");
        }
        Kind::NoFunction => {
            w.mark("takes no function", None);
            w.text.push_str("with bad = plain(\"p\")\n");
        }
        Kind::TooMany => {
            w.mark("parameter", None);
            w.text.push_str("with bad = wrap(\"w\")\n");
        }
        Kind::TooFew => {
            w.mark("parameter", None);
            w.text.push_str("with each(\"e\")\n");
        }
        _ => unreachable!("placed in a function"),
    }
}

fn write(m: &Model, style: Style) -> Writer {
    let mut w = Writer {
        text: String::from(HELPERS),
        style,
        mistake: None,
    };
    w.text.push('\n');
    for (i, f) in m.r.iter().enumerate() {
        let bad = match m.mistake {
            Some((kind, at)) if i == 0 && placed_in_function(kind) => Some((kind, at)),
            _ => None,
        };
        function(&mut w, Fam::R, f, i, bad);
    }
    for (i, f) in m.o.iter().enumerate() {
        function(&mut w, Fam::O, f, i, None);
    }
    function(&mut w, Fam::M, &m.m, 0, None);
    w.text.push_str("fn main() ! console, alloc {\n");
    for c in &m.calls {
        let (name, show) = match c.fam {
            Fam::R => (format!("r{}", c.f), "show"),
            Fam::O => (format!("o{}", c.f), "show_o"),
            Fam::M => ("run".to_string(), "show_m"),
        };
        let x = Arg::Lit(c.a.clone()).text(c.fam);
        let y = Arg::Lit(c.b.clone()).text(c.fam);
        w.line(2, &format!("{show}({name}({x}, {y}))"));
    }
    items(&mut w, m, 0, 2, &mut Vec::new());
    w.text.push_str("}\n");
    w
}

fn placed_in_function(kind: Kind) -> bool {
    matches!(kind, Kind::Dropped | Kind::NoTry | Kind::CannotHold)
}

fn build(g: &mut Gen, kind: Option<Kind>) -> TryCase {
    let m = model(g, kind);
    let sugar = write(&m, Style::Sugar);
    assert_eq!(sugar.mistake.is_some(), kind.is_some(), "{kind:?}");
    let (matched, lambdas, stdout) = if kind.is_none() {
        (
            write(&m, Style::Match).text,
            write(&m, Style::Lambdas).text,
            stdout(&m),
        )
    } else {
        (String::new(), String::new(), Vec::new())
    };
    TryCase {
        source: sugar.text,
        matched,
        lambdas,
        stdout,
        mistake: sugar.mistake,
    }
}
