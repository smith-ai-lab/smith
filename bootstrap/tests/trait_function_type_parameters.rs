//! A function of a trait takes type parameters of its own, with bounds. A call
//! settles them from its arguments as a call of a generic function does, an
//! implementation is checked once with each standing for any type that meets
//! its bound, and the program prints what it prints when the trait's function
//! is written as a generic function for each type.
//!
//! The generator chooses the shape of the trait's one function, which takes
//! 0 to 2 type parameters of its own, with a bound and without, the types
//! that implement it, and the calls `main` makes. It works out what the
//! program prints, and writes the program out a second time with the trait's
//! function as one generic function for each type.

use smith::check;
use smith::diag::Error;
use smith::{lower, scalar};

use crate::Test;
use crate::generate::{self, Gen};
use crate::programs::pos_after;

pub const ONCE: &[Test] = &[
    (
        "a_function_of_a_trait_settles_its_own_type_parameters_from_the_arguments",
        a_function_of_a_trait_settles_its_own_type_parameters_from_the_arguments,
    ),
    (
        "a_program_with_a_function_of_a_trait_that_takes_type_parameters_prints_what_the_generic_functions_print",
        a_program_with_a_function_of_a_trait_that_takes_type_parameters_prints_what_the_generic_functions_print,
    ),
    (
        "a_function_of_a_trait_with_a_type_parameter_is_used_through_a_bound",
        a_function_of_a_trait_with_a_type_parameter_is_used_through_a_bound,
    ),
    (
        "a_call_with_a_type_that_lacks_the_bound_is_one_error_at_the_call",
        a_call_with_a_type_that_lacks_the_bound_is_one_error_at_the_call,
    ),
    (
        "an_implementation_with_other_type_parameters_than_the_trait_is_one_error_at_the_function",
        an_implementation_with_other_type_parameters_than_the_trait_is_one_error_at_the_function,
    ),
    (
        "a_call_with_a_type_that_lacks_copy_or_discard_asked_of_a_type_parameter_of_a_function_of_a_trait_is_one_error_at_the_call",
        a_call_with_a_type_that_lacks_copy_or_discard_asked_of_a_type_parameter_of_a_function_of_a_trait_is_one_error_at_the_call,
    ),
    (
        "a_value_of_the_type_parameter_of_a_function_of_a_trait_is_given_away_once",
        a_value_of_the_type_parameter_of_a_function_of_a_trait_is_given_away_once,
    ),
];

/// The errors a program gives, which are none when it is accepted.
fn errors(source: &str) -> Vec<Error> {
    match check(source.as_bytes()) {
        Ok(_) => Vec::new(),
        Err(errors) => errors,
    }
}

/// What a program prints.
fn prints(source: &str) -> String {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}\n{source}"));
    String::from_utf8(scalar::run(&program).stdout).expect("text")
}

/// Two types that have `Sink`, which `put` writes a number to. A `Count`
/// adds it to what it holds, and a `Log` keeps the last. `Plain` has none.
const SINKS: &str = "struct Count {
  total: u64
}

struct Log {
  last: u64
}

struct Plain {
  n: u64
}

trait Sink[W] {
  fn put(w: take W, n: u64) -> W
}

impl Sink[Count] {
  fn put(w: take Count, n: u64) -> Count { Count(total: wrapping_add(w.total, n)) }
}

impl Sink[Log] {
  fn put(w: take Log, n: u64) -> Log { Log(last: n) }
}

";

// What the issue says, as it says it.

fn a_function_of_a_trait_settles_its_own_type_parameters_from_the_arguments() {
    let source = format!(
        "{SINKS}trait Emit[T] {{
  fn emit[W: Sink](value: T, to: take W) -> W
}}

impl Emit[u64] {{
  fn emit[W: Sink](value: u64, to: take W) -> W {{ put(to, value) }}
}}

fn main() ! console, alloc {{
  n: u64 = 7
  c = emit(n, Count(total: 1))
  l = emit(n, Log(last: 0))
  print(\"{{c.total}} {{l.last}}\")
}}
"
    );
    let found = errors(&source);
    assert!(found.is_empty(), "{found:?}\n{source}");
    assert_eq!(prints(&source), "8 7\n");
}

// The types a trait's function is implemented for, which each give a number
// to a sink, and a value of each.

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Of {
    U64,
    Bool,
    Text,
}

impl Of {
    const ALL: [Of; 3] = [Of::U64, Of::Bool, Of::Text];

    fn name(self) -> &'static str {
        match self {
            Of::U64 => "u64",
            Of::Bool => "bool",
            Of::Text => "str",
        }
    }

    /// A value of the type, from a number, as source.
    fn value(self, n: u64) -> String {
        match self {
            Of::U64 => n.to_string(),
            Of::Bool => (n % 2 == 1).to_string(),
            Of::Text => format!("\"{}\"", "a".repeat(n as usize % 3)),
        }
    }

    /// The number the value gives a sink, which `weigh` says.
    fn weight(self, n: u64) -> u64 {
        match self {
            Of::U64 => n,
            Of::Bool => u64::from(n % 2 == 1),
            Of::Text => n % 3,
        }
    }
}

/// `weigh`, which says the number a value of each type gives a sink.
const WEIGH: &str = "trait Weigh[T] {
  fn weigh(x: T) -> u64
}

impl Weigh[u64] {
  fn weigh(x: u64) -> u64 { x }
}

impl Weigh[bool] {
  fn weigh(x: bool) -> u64 { if x { 1 } else { 0 } }
}

impl Weigh[str] {
  fn weigh(x: str) -> u64 { if x.size == 0 { 0 } else if x.size == 1 { 1 } else { 2 } }
}

";

/// The shape of the trait's function: its own type parameters, its other
/// parameters, and what it gives, with what an implementation writes for it.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Shape {
    /// No type parameter.
    Plain,
    /// One with a bound.
    Sink,
    /// One with none, which is handed on.
    Keep,
    /// One of each, the bounded one first.
    Both,
    /// One of each, the one with none first.
    Flipped,
}

impl Shape {
    const ALL: [Shape; 5] = [
        Shape::Plain,
        Shape::Sink,
        Shape::Keep,
        Shape::Both,
        Shape::Flipped,
    ];

    fn generics(self) -> &'static str {
        match self {
            Shape::Plain => "",
            Shape::Sink => "[W: Sink]",
            Shape::Keep => "[X]",
            Shape::Both => "[W: Sink, X]",
            Shape::Flipped => "[X, W: Sink]",
        }
    }

    /// The parameters after `value`, and what it gives.
    fn rest(self) -> (&'static str, &'static str) {
        match self {
            Shape::Plain => (", n: u64", "u64"),
            Shape::Sink => (", to: take W", "W"),
            Shape::Keep => (", keep: take X", "X"),
            Shape::Both => (", to: take W, keep: take X", "(W, X)"),
            Shape::Flipped => (", keep: take X, to: take W", "(X, W)"),
        }
    }

    fn body(self) -> &'static str {
        match self {
            Shape::Plain => "wrapping_add(weigh(value), n)",
            Shape::Sink => "put(to, weigh(value))",
            Shape::Keep => "keep",
            Shape::Both => "(put(to, weigh(value)), keep)",
            Shape::Flipped => "(keep, put(to, weigh(value)))",
        }
    }

    /// The function with the type `value` has, written with `name`.
    fn function(self, name: &str, value: &str) -> String {
        let (rest, gives) = self.rest();
        format!("fn {name}{}(value: {value}{rest}) -> {gives}", self.generics())
    }

    fn sinks(self) -> bool {
        !matches!(self, Shape::Plain | Shape::Keep)
    }

    fn kept(self) -> bool {
        matches!(self, Shape::Keep | Shape::Both | Shape::Flipped)
    }
}

/// What a call hands to the sink and to the type parameter with no bound.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Sunk {
    Count,
    Log,
}

impl Sunk {
    /// The sink with `n` in it, as source.
    fn make(self, n: u64) -> String {
        match self {
            Sunk::Count => format!("Count(total: {n})"),
            Sunk::Log => format!("Log(last: {n})"),
        }
    }

    /// The field that says what it holds.
    fn field(self) -> &'static str {
        match self {
            Sunk::Count => "total",
            Sunk::Log => "last",
        }
    }

    /// What it holds after a number is put to it, when it held `n`.
    fn put(self, n: u64, weight: u64) -> u64 {
        match self {
            Sunk::Count => n + weight,
            Sunk::Log => weight,
        }
    }
}

/// A call of the trait's function in `main`.
#[derive(Clone, Debug)]
struct Call {
    of: Of,
    /// The number the value of the type is made from.
    value: u64,
    sink: Sunk,
    /// What the sink holds before the call.
    held: u64,
    /// Whether the type parameter with no bound is a text or a number.
    keep_text: bool,
}

impl Call {
    fn keep(&self) -> String {
        if self.keep_text {
            format!("\"k{}\"", self.held)
        } else {
            self.held.to_string()
        }
    }

    fn keep_value(&self) -> String {
        if self.keep_text {
            format!("k{}", self.held)
        } else {
            self.held.to_string()
        }
    }
}

#[derive(Clone, Debug)]
struct Program {
    shape: Shape,
    /// The types the function is implemented for.
    implemented: Vec<Of>,
    calls: Vec<Call>,
}

fn generate_program(g: &mut Gen) -> Program {
    let shape = *g.pick(&Shape::ALL);
    let mut implemented = vec![*g.pick(&Of::ALL)];
    for of in Of::ALL {
        if !implemented.contains(&of) && g.one_in(2) {
            implemented.push(of);
        }
    }
    let calls = (0..1 + g.below(3))
        .map(|_| Call {
            of: *g.pick(&implemented),
            value: g.below(5) as u64,
            sink: *g.pick(&[Sunk::Count, Sunk::Log]),
            held: g.below(9) as u64,
            keep_text: g.one_in(2),
        })
        .collect();
    Program {
        shape,
        implemented,
        calls,
    }
}

impl Program {
    /// The source of the call, and what the program prints for it.
    fn call(&self, at: usize, call: &Call, name: &dyn Fn(Of) -> String) -> (String, String) {
        // The value is bound with its type, as a number alone is an `i64`.
        let value = format!("v{at}");
        let bind = format!("  {value}: {} = {}\n", call.of.name(), call.of.value(call.value));
        let callee = name(call.of);
        let sink = call.sink.make(call.held);
        let keep = call.keep();
        let weight = call.of.weight(call.value);
        let after = call.sink.put(call.held, weight);
        let field = call.sink.field();
        let (text, says) = match self.shape {
            Shape::Plain => (
                format!("  r{at} = {callee}({value}, {})\n  print(\"{{r{at}}}\")\n", call.held),
                format!("{}\n", weight + call.held),
            ),
            Shape::Sink => (
                format!("  r{at} = {callee}({value}, {sink})\n  print(\"{{r{at}.{field}}}\")\n"),
                format!("{after}\n"),
            ),
            Shape::Keep => (
                format!("  r{at} = {callee}({value}, {keep})\n  print(\"{{r{at}}}\")\n"),
                format!("{}\n", call.keep_value()),
            ),
            Shape::Both => (
                format!(
                    "  (w{at}, x{at}) = {callee}({value}, {sink}, {keep})\n  print(\"{{w{at}.{field}}} {{x{at}}}\")\n"
                ),
                format!("{after} {}\n", call.keep_value()),
            ),
            Shape::Flipped => (
                format!(
                    "  (x{at}, w{at}) = {callee}({value}, {keep}, {sink})\n  print(\"{{x{at}}} {{w{at}.{field}}}\")\n"
                ),
                format!("{} {after}\n", call.keep_value()),
            ),
        };
        (format!("{bind}{text}"), says)
    }

    /// The program, with the trait's function a function of a trait, and
    /// what it prints.
    fn source(&self) -> (String, String) {
        let shape = self.shape;
        let mut source = format!("{SINKS}{WEIGH}trait Emit[T] {{\n  {}\n}}\n\n", shape.function("emit", "T"));
        for of in &self.implemented {
            source.push_str(&format!(
                "impl Emit[{0}] {{\n  {1} {{ {2} }}\n}}\n\n",
                of.name(),
                shape.function("emit", of.name()),
                shape.body()
            ));
        }
        let (main, printed) = self.main(&|_| "emit".to_string());
        source.push_str(&main);
        (source, printed)
    }

    /// The program with a generic function for each type that has the
    /// function, and no trait.
    fn written_out(&self) -> String {
        let shape = self.shape;
        let mut source = format!("{SINKS}{WEIGH}");
        for of in &self.implemented {
            source.push_str(&format!(
                "{} {{ {} }}\n\n",
                shape.function(&format!("emit_{}", of.name()), of.name()),
                shape.body()
            ));
        }
        source.push_str(&self.main(&|of| format!("emit_{}", of.name())).0);
        source
    }

    fn main(&self, name: &dyn Fn(Of) -> String) -> (String, String) {
        let mut main = "fn main() ! console, alloc {\n".to_string();
        let mut printed = String::new();
        for (at, call) in self.calls.iter().enumerate() {
            let (text, says) = self.call(at, call, name);
            main.push_str(&text);
            printed.push_str(&says);
        }
        main.push_str("}\n");
        (main, printed)
    }
}

fn a_program_with_a_function_of_a_trait_that_takes_type_parameters_prints_what_the_generic_functions_print()
 {
    generate::check(4471, 40, generate_program, |program| {
        let (source, printed) = program.source();
        let found = errors(&source);
        assert!(found.is_empty(), "{found:?}\n{source}");
        assert_eq!(prints(&source), printed, "{source}");
        let written = program.written_out();
        assert_eq!(prints(&written), printed, "{written}");
    });
}

// A function of a trait that is used through a bound.

fn a_function_of_a_trait_with_a_type_parameter_is_used_through_a_bound() {
    let generate = |g: &mut Gen| {
        let of = *g.pick(&Of::ALL);
        let values: Vec<u64> = (0..1 + g.below(3)).map(|_| g.below(5) as u64).collect();
        (of, values, *g.pick(&[Sunk::Count, Sunk::Log]), g.below(9) as u64)
    };
    generate::check(4472, 40, generate, |(of, values, sink, held)| {
        let ty = of.name();
        let items: Vec<String> = values.iter().map(|v| of.value(*v)).collect();
        let source = format!(
            "{SINKS}{WEIGH}trait Emit[T] {{
  fn emit[W: Sink](value: T, to: take W) -> W
}}

impl Emit[{ty}] {{
  fn emit[W: Sink](value: {ty}, to: take W) -> W {{ put(to, weigh(value)) }}
}}

fn all[T: Emit, W: Sink](xs: List[T], to: take W) -> W {{
  var w = to
  loop i in 0..xs.size {{
    w = emit(xs[i], w)
  }}
  w
}}

fn main() ! console, alloc {{
  xs: List[{ty}] = [{}]
  r = all(xs, {})
  print(\"{{r.{}}}\")
}}
",
            items.join(", "),
            sink.make(*held),
            sink.field()
        );
        let expected = values
            .iter()
            .fold(*held, |held, v| sink.put(held, of.weight(*v)));
        let found = errors(&source);
        assert!(found.is_empty(), "{found:?}\n{source}");
        assert_eq!(prints(&source), format!("{expected}\n"), "{source}");
    });
}

// A call with a type that lacks the bound.

fn a_call_with_a_type_that_lacks_the_bound_is_one_error_at_the_call() {
    let generate = |g: &mut Gen| {
        let bounded: Vec<Shape> = Shape::ALL.iter().copied().filter(|s| s.sinks()).collect();
        (*g.pick(&bounded), *g.pick(&Of::ALL), g.below(5) as u64)
    };
    generate::check(4473, 30, generate, |(shape, of, n)| {
        let ty = of.name();
        let keep = if shape.kept() { ", 1" } else { "" };
        let (before, after) = match shape {
            Shape::Flipped => ("1, ", ""),
            _ => ("", keep),
        };
        let mut source = format!(
            "{SINKS}{WEIGH}trait Emit[T] {{\n  {}\n}}\n\nimpl Emit[{ty}] {{\n  {} {{ {} }}\n}}\n\nfn main() ! console, alloc {{\n  v: {ty} = {}\n  r = ",
            shape.function("emit", "T"),
            shape.function("emit", ty),
            shape.body(),
            of.value(*n)
        );
        let at = pos_after(&source);
        source.push_str(&format!(
            "emit(v, {before}Plain(n: 1){after})\n}}\n"
        ));
        let found = errors(&source);
        assert_eq!(found.len(), 1, "{found:?}\n{source}");
        assert_eq!(found[0].pos, at, "{source}");
        assert!(found[0].message.contains("`Sink`"), "{}", found[0]);
        assert!(found[0].message.contains("`Plain`"), "{}", found[0]);
    });
}

// An implementation that writes other type parameters than the trait.

fn an_implementation_with_other_type_parameters_than_the_trait_is_one_error_at_the_function() {
    let generate = |g: &mut Gen| {
        let generic: Vec<Shape> = Shape::ALL
            .iter()
            .copied()
            .filter(|s| !s.generics().is_empty())
            .collect();
        (*g.pick(&generic), g.below(3))
    };
    generate::check(4474, 30, generate, |(shape, change)| {
        let written = shape.generics();
        // What the implementation writes instead of the trait's.
        let wrong = match *change {
            // A name that is not the trait's.
            0 => written.replace('W', "V").replace('X', "Y"),
            // No bound where the trait has one, or a bound where it has none.
            1 => written.replace(": Sink", "").replace('X', "X: Sink"),
            // None at all.
            _ => String::new(),
        };
        if wrong == written {
            return;
        }
        let trait_text = format!(
            "{SINKS}{WEIGH}trait Emit[T] {{\n  {}\n}}\n\nimpl Emit[u64] {{\n  fn emit",
            shape.function("emit", "T")
        );
        let (rest, gives) = shape.rest();
        let renamed = |t: &str| t.replace('W', "V").replace('X', "Y");
        let (rest, gives) = if *change == 0 {
            (renamed(rest), renamed(gives))
        } else {
            (rest.to_string(), gives.to_string())
        };
        let mut source = trait_text;
        let at = pos_after(&format!("{source}{}", if wrong.is_empty() { "" } else { "[" }));
        let at = if wrong.is_empty() {
            pos_after(&source[..source.len() - 4])
        } else {
            at
        };
        source.push_str(&format!(
            "{wrong}(value: u64{rest}) -> {gives} {{ {} }}\n}}\n\nfn main() ! console, alloc {{\n  n: u64 = 1\n  r = emit(n, {})\n}}\n",
            shape.body().replace("weigh(value)", "value"),
            match shape {
                Shape::Plain => "1".to_string(),
                Shape::Sink => "Count(total: 1)".to_string(),
                Shape::Keep => "1".to_string(),
                Shape::Both => "Count(total: 1), 1".to_string(),
                Shape::Flipped => "1, Count(total: 1)".to_string(),
            }
        ));
        let found = errors(&source);
        let own: Vec<&Error> = found
            .iter()
            .filter(|e| e.message.contains("type parameters") || e.message.contains("`emit` has"))
            .collect();
        assert_eq!(own.len(), 1, "{found:?}\n{source}");
        assert_eq!(own[0].pos, at, "{source}");
        assert!(own[0].message.contains(written), "{}", own[0]);
        if !wrong.is_empty() {
            assert!(own[0].message.contains(&wrong), "{}", own[0]);
        }
    });
}

// What is known of a type parameter of a function of a trait.

fn a_value_of_the_type_parameter_of_a_function_of_a_trait_is_given_away_once() {
    let source = |body: &str| {
        format!(
            "{SINKS}trait Emit[T] {{
  fn emit[W: Sink](value: T, to: take W) -> W
}}

impl Emit[u64] {{
  fn emit[W: Sink](value: u64, to: take W) -> W {{
{body}  }}
}}

fn main() ! console, alloc {{
  n: u64 = 1
  c = emit(n, Count(total: 0))
  print(\"{{c.total}}\")
}}
"
        )
    };
    let once = source("    put(to, value)\n");
    assert!(errors(&once).is_empty(), "{:?}", errors(&once));
    let twice = source("    var w = put(to, value)\n    w = put(to, value)\n    w\n");
    let at = {
        let first = twice.find("put(to, value)").unwrap();
        let second = twice[first + 1..].find("put(to, value)").unwrap() + first + 1;
        pos_after(&twice[..second + "put(".len()])
    };
    let found = errors(&twice);
    assert!(
        found
            .iter()
            .any(|e| e.pos == at && e.message.contains("`to`")),
        "{found:?}\n{twice}"
    );
}

// `Copy` and `Discard` asked of a type parameter of a function of a trait.

/// A type a type parameter may be given: `Key` has `Discard` and not `Copy`,
/// `Soft` has `Copy` and not `Discard`, and `Raw` has neither.
const KINDS: &str = "struct Key {
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

";

fn a_call_with_a_type_that_lacks_copy_or_discard_asked_of_a_type_parameter_of_a_function_of_a_trait_is_one_error_at_the_call()
 {
    // The type, what it has, and the statement that makes a value named `v`.
    let kinds = [
        ("u64", true, true, "v: u64 = 1"),
        ("Key", false, true, "v = Key(secret: 1)"),
        ("Soft", true, false, "v = Soft(n: 1)"),
        ("Raw", false, false, "v = Raw(n: 1)"),
    ];
    let generate = |g: &mut Gen| (*g.pick(&["Copy", "Discard"]), *g.pick(&kinds), g.one_in(2));
    generate::check(4475, 40, generate, |(bound, (ty, copy, discard, make), through)| {
        let has = if *bound == "Copy" { *copy } else { *discard };
        let head = format!(
            "{KINDS}trait Pass[T] {{
  fn pass[W: {bound}](value: T, w: W) -> u64
}}

impl Pass[u64] {{
  fn pass[W: {bound}](value: u64, w: W) -> u64 {{ 1 }}
}}

"
        );
        // The call is in `main`, or in a function that is bound by `Pass`
        // and asks for the bound of its own type parameter, or does not.
        let (mut source, callee) = if *through {
            (format!("{head}fn via[T: Pass, W](x: T, w: W) -> u64 {{\n  "), "via")
        } else {
            (format!("{head}fn main() {{\n  n: u64 = 1\n  {make}\n  r = "), "pass")
        };
        let at = pos_after(&source);
        if *through {
            // Inside `via`, `W` has only what `via` asks of it, which is nothing.
            source.push_str(&format!(
                "pass(x, w)\n}}\n\nfn main() {{\n  n: u64 = 1\n  {make}\n  r = via(n, v)\n  s = r\n}}\n"
            ));
            let found = errors(&source);
            assert_eq!(found.len(), 1, "{found:?}\n{source}");
            assert_eq!(found[0].pos, at, "{source}");
            assert!(found[0].message.contains("`W`"), "{}", found[0]);
            assert!(found[0].message.contains(bound), "{}", found[0]);
            return;
        }
        source.push_str(&format!("{callee}(n, v)\n  s = r\n}}\n"));
        let found = errors(&source);
        if has {
            assert!(found.is_empty(), "{found:?}\n{source}");
        } else {
            assert_eq!(found.len(), 1, "{found:?}\n{source}");
            assert_eq!(found[0].pos, at, "{source}");
            assert!(found[0].message.contains(&format!("`{ty}`")), "{}", found[0]);
            assert!(found[0].message.contains(bound), "{}", found[0]);
        }
    });
}
