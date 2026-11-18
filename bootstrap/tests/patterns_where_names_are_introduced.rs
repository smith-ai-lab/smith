//! A tuple's pattern where a name is introduced: as a parameter of a
//! function, as a parameter of a lambda, and as the name of a `loop` over a
//! list. Every property compares a program that writes the pattern there
//! with the program that has a plain name there and binds the pattern as the
//! first line of the body, and takes what the program must do from the
//! generator that built the tuple.

use smith::diag::Pos;
use smith::scalar::{self, Outcome};
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "the_examples_of_the_page_on_tuples_for_a_parameter_and_a_loop_give_what_it_says",
        the_examples_of_the_page_on_tuples_for_a_parameter_and_a_loop_give_what_it_says,
    ),
    (
        "a_pattern_where_a_name_is_introduced_does_what_the_binding_of_it_on_the_first_line_does",
        a_pattern_where_a_name_is_introduced_does_what_the_binding_of_it_on_the_first_line_does,
    ),
    (
        "a_pattern_where_a_name_is_introduced_adds_nothing_to_the_program",
        a_pattern_where_a_name_is_introduced_adds_nothing_to_the_program,
    ),
    (
        "a_pattern_that_may_not_match_where_a_name_is_introduced_is_one_error_that_names_match",
        a_pattern_that_may_not_match_where_a_name_is_introduced_is_one_error_that_names_match,
    ),
    (
        "a_pattern_of_the_wrong_size_or_on_no_tuple_gives_the_errors_its_binding_gives",
        a_pattern_of_the_wrong_size_or_on_no_tuple_gives_the_errors_its_binding_gives,
    ),
    (
        "a_parameter_written_as_a_pattern_is_given_by_position_and_takes_no_default",
        a_parameter_written_as_a_pattern_is_given_by_position_and_takes_no_default,
    ),
    (
        "a_name_a_pattern_introduces_is_owned_and_lent_as_any_other_name_is",
        a_name_a_pattern_introduces_is_owned_and_lent_as_any_other_name_is,
    ),
    (
        "each_mistake_in_a_call_of_a_function_with_pattern_parameters_is_one_error_that_holds_no_at_sign",
        each_mistake_in_a_call_of_a_function_with_pattern_parameters_is_one_error_that_holds_no_at_sign,
    ),
];

#[derive(Clone, Copy, Debug, PartialEq)]
enum Place {
    Function,
    Lambda,
    Loop,
}

const PLACES: [Place; 3] = [Place::Function, Place::Lambda, Place::Loop];

#[derive(Clone, Debug)]
struct Leaf {
    name: String,
    /// Whether the name is marked `var`.
    var: bool,
    /// Whether the leaf is a `Key`, which has no `Copy`, and not an `i32`.
    key: bool,
    value: i32,
    /// How many times the body reads the name.
    uses: usize,
}

/// The shape of a tuple, with the names its pattern gives its leaves.
#[derive(Clone, Debug)]
enum Shape {
    Leaf(Leaf),
    Tuple(Vec<Shape>),
}

impl Shape {
    fn leaves<'a>(&'a self, out: &mut Vec<&'a Leaf>) {
        match self {
            Shape::Leaf(l) => out.push(l),
            Shape::Tuple(items) => items.iter().for_each(|i| i.leaves(out)),
        }
    }

    fn all(&self) -> Vec<&Leaf> {
        let mut out = Vec::new();
        self.leaves(&mut out);
        out
    }

    fn ty(&self) -> String {
        match self {
            Shape::Leaf(l) if l.key => "Key".into(),
            Shape::Leaf(_) => "i32".into(),
            Shape::Tuple(items) => {
                let items: Vec<String> = items.iter().map(Shape::ty).collect();
                format!("({})", items.join(", "))
            }
        }
    }

    fn pattern(&self) -> String {
        match self {
            Shape::Leaf(l) if l.var => format!("var {}", l.name),
            Shape::Leaf(l) => l.name.clone(),
            Shape::Tuple(items) => {
                let items: Vec<String> = items.iter().map(Shape::pattern).collect();
                format!("({})", items.join(", "))
            }
        }
    }

    fn value(&self) -> String {
        match self {
            Shape::Leaf(l) if l.key => format!("Key(secret: {})", l.value),
            Shape::Leaf(l) => l.value.to_string(),
            Shape::Tuple(items) => {
                let items: Vec<String> = items.iter().map(Shape::value).collect();
                format!("({})", items.join(", "))
            }
        }
    }

    /// What the body gives: the sum of every read of every name, and for a
    /// `var` the amount the body added to it first.
    fn sum(&self) -> i32 {
        self.all()
            .iter()
            .map(|l| {
                let bumped = if l.var { BUMP } else { 0 };
                if l.key {
                    3 * l.uses as i32
                } else {
                    (l.value + bumped) * l.uses as i32
                }
            })
            .sum()
    }
}

const BUMP: i32 = 100;

/// A tuple of two or three parts, whose parts may be tuples, to a depth.
fn shape(g: &mut Gen, keys: bool) -> Shape {
    fn build(g: &mut Gen, depth: usize, top: bool, count: &mut usize, keys: bool) -> Shape {
        if depth == 0 || (!top && g.one_in(3)) {
            let name = format!("n{count}");
            *count += 1;
            let key = keys && g.one_in(2);
            return Shape::Leaf(Leaf {
                name,
                var: !keys && g.one_in(3),
                key,
                value: g.below(40) as i32,
                uses: 1 + usize::from(keys && g.one_in(3)),
            });
        }
        let size = 2 + g.below(2);
        Shape::Tuple(
            (0..size)
                .map(|_| build(g, depth - 1, false, count, keys))
                .collect(),
        )
    }
    let depth = 1 + g.below(3);
    build(g, depth, true, &mut 0, keys)
}

/// The same shape with new values.
fn revalue(g: &mut Gen, s: &Shape) -> Shape {
    match s {
        Shape::Leaf(l) => Shape::Leaf(Leaf {
            value: g.below(40) as i32,
            ..l.clone()
        }),
        Shape::Tuple(items) => Shape::Tuple(items.iter().map(|i| revalue(g, i)).collect()),
    }
}

const KEY: &str = "struct Key {\n  secret: i32\n}\n\nwithout Copy for Key\n\nfn eat(k: take Key) -> i32 { 3 }\n\n";

/// What the body of the place says first and then gives: the lines that
/// change each `var`, and the sum of the reads.
fn body(s: &Shape) -> (Vec<String>, String) {
    let bumps = (s.all().iter().filter(|l| l.var))
        .map(|l| format!("{n} = wrapping_add({n}, {BUMP})", n = l.name))
        .collect();
    let mut terms = Vec::new();
    for l in s.all() {
        for _ in 0..l.uses {
            terms.push(if l.key {
                format!("eat({})", l.name)
            } else {
                l.name.clone()
            });
        }
    }
    let mut sum = terms[0].clone();
    for t in &terms[1..] {
        sum = format!("wrapping_add({sum}, {t})");
    }
    (bumps, sum)
}

/// A program that writes the shape's pattern in the place, or that writes a
/// name there and binds the pattern on the first line. `take` marks the
/// parameter that holds the tuple, where the place has one that can be
/// marked.
fn program(place: Place, (s, given): (&Shape, &Shape), other: &Shape, written_out: bool, take: bool) -> String {
    let (bumps, sum) = body(s);
    let ty = given.ty();
    let head = if written_out {
        "arg".to_string()
    } else {
        s.pattern()
    };
    let first = if written_out {
        format!("{} = arg\n", s.pattern())
    } else {
        String::new()
    };
    let mark = if take { "take " } else { "" };
    let lines = |indent: &str| -> String {
        bumps.iter().map(|b| format!("{indent}{b}\n")).collect()
    };
    let mut out = String::new();
    if s.all().iter().any(|l| l.key) || other.all().iter().any(|l| l.key) {
        out.push_str(KEY);
    }
    match place {
        Place::Function => {
            out.push_str(&format!(
                "fn f({head}: {mark}{ty}) -> i32 {{\n{first}{}  {sum}\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{f({})}}\")\n}}\n",
                lines("  "),
                given.value()
            ));
        }
        Place::Lambda => {
            out.push_str(&format!(
                "fn apply(v: {mark}{ty}, f: ({mark}{ty}) -> i32) -> i32 {{ f(v) }}\n\nfn main() ! console, alloc {{\n  r = apply({}) fn({head}) {{\n{first}{}    {sum}\n  }}\n  print(\"{{r}}\")\n}}\n",
                given.value(),
                lines("    ")
            ));
        }
        Place::Loop => {
            out.push_str(&format!(
                "fn total(xs: List[{ty}]) -> i32 {{\n  var sum = 0\n  loop {head} in xs {{\n{first}{}    sum = wrapping_add(sum, {sum})\n  }}\n  sum\n}}\n\nfn main() ! console, alloc {{\n  xs: List[{ty}] = [{}, {}]\n  print(\"{{total(xs)}}\")\n}}\n",
                lines("    "),
                given.value(),
                other.value()
            ));
        }
    }
    out
}

/// What the program of the place prints.
fn printed(place: Place, s: &Shape, other: &Shape) -> String {
    let n = match place {
        Place::Loop => s.sum() + other.sum(),
        _ => s.sum(),
    };
    format!("{n}\n")
}

fn run(source: &str) -> Outcome {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
    scalar::run(&program)
}

/// The messages of the errors a program is refused with, or none.
fn refused(source: &str) -> Vec<String> {
    match check(source.as_bytes()) {
        Ok(_) => Vec::new(),
        Err(errors) => errors.into_iter().map(|e| e.message).collect(),
    }
}

/// The messages with the places they name taken out, as the written-out form
/// has a line more.
fn without_places(messages: Vec<String>) -> Vec<String> {
    let place = |c: char| c.is_ascii_digit() || c == ':';
    messages
        .into_iter()
        .map(|m| {
            let mut out = String::new();
            let mut chars = m.chars().peekable();
            while let Some(c) = chars.next() {
                if c.is_ascii_digit() {
                    let mut run = c.to_string();
                    while chars.peek().is_some_and(|&n| place(n)) {
                        run.push(chars.next().unwrap());
                    }
                    out.push_str(if run.contains(':') { "<place>" } else { &run });
                } else {
                    out.push(c);
                }
            }
            out
        })
        .collect()
}

fn the_examples_of_the_page_on_tuples_for_a_parameter_and_a_loop_give_what_it_says() {
    let source = "fn first((n, s): (i32, str)) -> i32 { n }\n\nfn total(pairs: List[(str, i32)]) -> i32 {\n  var sum = 0\n  loop (name, count) in pairs { sum = wrapping_add(sum, count) }\n  sum\n}\n\nfn main() ! console, alloc {\n  pairs: List[(str, i32)] = [(\"a\", 4), (\"b\", 5)]\n  print(\"{first((7, \"x\"))}\")\n  print(\"{total(pairs)}\")\n}\n";
    assert_eq!(
        run(source),
        Outcome {
            stdout: b"7\n9\n".to_vec(),
            status: 0
        },
        "for {source}"
    );
}

/// For every place, depth of nesting, and mark of `var`, the program prints
/// what the sum of the values is, and what its written-out form prints.
fn a_pattern_where_a_name_is_introduced_does_what_the_binding_of_it_on_the_first_line_does() {
    generate::check(
        101,
        24,
        |g| {
            let s = shape(g, false);
            let other = revalue(g, &s);
            (s, other)
        },
        |(s, other)| {
            for place in PLACES {
                let want = Outcome {
                    stdout: printed(place, s, other).into_bytes(),
                    status: 0,
                };
                for written_out in [false, true] {
                    let source = program(place, (s, s), other, written_out, false);
                    assert_eq!(run(&source), want, "{place:?} for\n{source}");
                }
            }
        },
    );
}

fn a_pattern_where_a_name_is_introduced_adds_nothing_to_the_program() {
    generate::check(
        102,
        24,
        |g| {
            let s = shape(g, false);
            let other = revalue(g, &s);
            (s, other)
        },
        |(s, other)| {
            for place in PLACES {
                let with = lower(program(place, (s, s), other, false, false).as_bytes());
                let without = lower(program(place, (s, s), other, true, false).as_bytes());
                assert!(with.is_ok(), "{place:?}: {with:?}");
                assert_eq!(with, without, "{place:?} for {s:?}");
            }
        },
    );
}

/// A case, a literal, and a list, alone or inside a tuple, in each place.
fn a_pattern_that_may_not_match_where_a_name_is_introduced_is_one_error_that_names_match() {
    const BAD: [&str; 4] = [".some(a)", "3", "\"s\"", "[a, b]"];
    generate::check(
        103,
        40,
        |g| (*g.pick(&PLACES), *g.pick(&BAD), g.one_in(2), g.one_in(2)),
        |&(place, bad, inside, last)| {
            let pattern = match (inside, last) {
                (false, _) => bad.to_string(),
                (true, false) => format!("({bad}, b)"),
                (true, true) => format!("(a, {bad})"),
            };
            let source = match place {
                Place::Function => format!(
                    "fn f({pattern}: (i32, i32)) -> i32 {{ 1 }}\n\nfn main() ! console, alloc {{\n  print(\"{{f((1, 2))}}\")\n}}\n"
                ),
                Place::Lambda => format!(
                    "fn apply(v: (i32, i32), f: ((i32, i32)) -> i32) -> i32 {{ f(v) }}\n\nfn main() ! console, alloc {{\n  r = apply((1, 2)) fn({pattern}) {{ 1 }}\n  print(\"{{r}}\")\n}}\n"
                ),
                Place::Loop => format!(
                    "fn main() ! console, alloc {{\n  xs: List[(i32, i32)] = [(1, 2)]\n  loop {pattern} in xs {{ print(\"1\") }}\n}}\n"
                ),
            };
            let errors = refused(&source);
            assert_eq!(errors.len(), 1, "{errors:?} for\n{source}");
            assert!(
                errors[0].contains("`match`") && errors[0].contains("may not match"),
                "{errors:?} for\n{source}"
            );
        },
    );
}

/// A pattern with one name too many, and a pattern on a number, give what
/// the binding of the pattern gives.
fn a_pattern_of_the_wrong_size_or_on_no_tuple_gives_the_errors_its_binding_gives() {
    generate::check(
        104,
        24,
        |g| (shape(g, false), g.one_in(2)),
        |(s, no_tuple)| {
            let (pattern, given) = if *no_tuple {
                // The same pattern on a number.
                let leaf = Shape::Leaf(Leaf {
                    name: "x".into(),
                    var: false,
                    key: false,
                    value: 1,
                    uses: 1,
                });
                (s.clone(), leaf)
            } else {
                // One name more than the tuple has.
                let Shape::Tuple(items) = s else {
                    panic!("a tuple at the top")
                };
                let mut more = items.clone();
                more.push(Shape::Leaf(Leaf {
                    name: "extra".into(),
                    var: false,
                    key: false,
                    value: 1,
                    uses: 1,
                }));
                (Shape::Tuple(more), s.clone())
            };
            for place in PLACES {
                let with = refused(&program(place, (&pattern, &given), &given, false, false));
                let without = refused(&program(place, (&pattern, &given), &given, true, false));
                assert!(
                    with.iter().any(|m| m.contains("takes apart a tuple of")),
                    "{place:?}: {with:?}"
                );
                assert_eq!(
                    without_places(with),
                    without_places(without),
                    "{place:?}"
                );
            }
        },
    );
}

/// A parameter written as a pattern has no name a call can give, and no
/// default.
fn a_parameter_written_as_a_pattern_is_given_by_position_and_takes_no_default() {
    generate::check(
        105,
        24,
        |g| shape(g, false),
        |s| {
            let (_, sum) = body(s);
            let (ty, pattern, value) = (s.ty(), s.pattern(), s.value());
            let header = format!("fn f({pattern}: {ty}");
            let main = |call: &str| {
                format!("\n\nfn main() ! console, alloc {{\n  print(\"{{{call}}}\")\n}}\n")
            };
            let built = |header: &str| {
                let lines: String = (s.all().iter().filter(|l| l.var))
                    .map(|l| format!("  {n} = wrapping_add({n}, {BUMP})\n", n = l.name))
                    .collect();
                format!("{header}) -> i32 {{\n{lines}  {sum}\n}}")
            };
            // By position, it is accepted.
            let by_position = built(&header) + &main(&format!("f({value})"));
            assert_eq!(refused(&by_position), Vec::<String>::new(), "{by_position}");
            // By any name, it is refused: no name can be written for it.
            for name in ["arg", "t", "pattern", &s.all()[0].name] {
                let by_name = built(&header) + &main(&format!("f({name}: {value})"));
                let errors = refused(&by_name);
                assert!(
                    errors.iter().any(|e| e.contains("has no parameter named")),
                    "{errors:?} for\n{by_name}"
                );
            }
            // With a default, it is refused.
            let defaulted = built(&format!("{header} = {value}")) + &main("f()");
            let errors = refused(&defaulted);
            assert_eq!(errors.len(), 1, "{errors:?} for\n{defaulted}");
            assert!(errors[0].contains("default"), "{errors:?}");
        },
    );
}

/// A tuple holding a `Key`, which has no `Copy`, is a parameter marked
/// `take`, a parameter of a lambda, or the element of a loop. Reading a
/// name of its pattern twice is refused exactly when it is a `Key`, and each
/// place gives the errors that its written-out form gives.
fn a_name_a_pattern_introduces_is_owned_and_lent_as_any_other_name_is() {
    generate::check(
        106,
        24,
        |g| {
            let s = shape(g, true);
            let other = revalue(g, &s);
            (s, other)
        },
        |(s, other)| {
            let twice: Vec<&str> = (s.all().iter())
                .filter(|l| l.key && l.uses == 2)
                .map(|l| l.name.as_str())
                .collect();
            let any_key = s.all().iter().any(|l| l.key);
            for place in PLACES {
                // A loop lends its element, so a `Key` read as owned is
                // refused wherever it is read.
                let take = place != Place::Loop;
                let with = refused(&program(place, (s, s), other, false, take));
                let without = refused(&program(place, (s, s), other, true, take));
                assert_eq!(
                    without_places(with.clone()),
                    without_places(without),
                    "{place:?} for {s:?}"
                );
                let refused_here = if place == Place::Loop {
                    any_key
                } else {
                    !twice.is_empty()
                };
                assert_eq!(!with.is_empty(), refused_here, "{place:?}: {with:?} for {s:?}");
                if place != Place::Loop {
                    for name in &twice {
                        assert!(
                            with.iter().any(|m| m.contains(&format!("`{name}`"))),
                            "{place:?}: {with:?} does not name `{name}`"
                        );
                    }
                }
            }
        },
    );
}

/// One parameter of a function: a plain name, or a pattern.
enum Param {
    Plain(String),
    Pattern(Shape),
}

impl Param {
    fn ty(&self) -> String {
        match self {
            Param::Plain(_) => "i32".into(),
            Param::Pattern(s) => s.ty(),
        }
    }

    fn head(&self) -> String {
        match self {
            Param::Plain(n) => n.clone(),
            Param::Pattern(s) => s.pattern(),
        }
    }

    fn value(&self) -> String {
        match self {
            Param::Plain(_) => "1".into(),
            Param::Pattern(s) => s.value(),
        }
    }

    fn names(&self) -> Vec<String> {
        match self {
            Param::Plain(n) => vec![n.clone()],
            Param::Pattern(s) => s.all().iter().map(|l| l.name.clone()).collect(),
        }
    }
}

/// The shape with each name given the prefix, and no `var`.
fn prefixed(s: &Shape, prefix: &str) -> Shape {
    match s {
        Shape::Leaf(l) => Shape::Leaf(Leaf {
            name: format!("{prefix}{}", l.name),
            var: false,
            ..l.clone()
        }),
        Shape::Tuple(items) => Shape::Tuple(items.iter().map(|i| prefixed(i, prefix)).collect()),
    }
}

/// A mistake in a call.
#[derive(Clone, Copy, Debug)]
enum Mistake {
    /// The last argument is left out.
    Missing,
    /// One argument more than there are parameters.
    OneMore,
    /// A last argument that names a parameter there is not.
    Named,
    /// The last argument, which is for a pattern, is given by a name.
    NamedInstead,
    /// One argument is text.
    WrongType(usize),
}

#[derive(Debug)]
struct CallCase {
    params: Vec<Param>,
    mistake: Mistake,
}

impl std::fmt::Debug for Param {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}: {}", self.head(), self.ty())
    }
}

fn call_case(g: &mut Gen) -> CallCase {
    let count = 1 + g.below(3);
    let mut params = Vec::new();
    for k in 0..count {
        if k == 0 || g.one_in(2) {
            params.push(Param::Pattern(prefixed(&shape(g, false), &format!("p{k}_"))));
        } else {
            params.push(Param::Plain(format!("p{k}")));
        }
    }
    let last_is_pattern = matches!(params.last(), Some(Param::Pattern(_)));
    let mistake = match g.below(5) {
        0 => Mistake::Missing,
        1 => Mistake::OneMore,
        2 => Mistake::Named,
        3 if last_is_pattern => Mistake::NamedInstead,
        3 => Mistake::Named,
        _ => Mistake::WrongType(g.below(count)),
    };
    CallCase { params, mistake }
}

/// Each mistake of a call of a function whose parameters are patterns is one
/// error, at the place of the mistake, whose words and advice hold no `@`,
/// which is in the name the compiler gives a pattern.
fn each_mistake_in_a_call_of_a_function_with_pattern_parameters_is_one_error_that_holds_no_at_sign()
{
    generate::check(107, 60, call_case, |case| {
        let names: Vec<String> = case.params.iter().flat_map(Param::names).collect();
        let mut sum = names[0].clone();
        for n in &names[1..] {
            sum = format!("wrapping_add({sum}, {n})");
        }
        let declared: Vec<String> = case
            .params
            .iter()
            .map(|p| format!("{}: {}", p.head(), p.ty()))
            .collect();
        let head = format!(
            "fn f({}) -> i32 {{ {sum} }}\n\nfn main() ! console, alloc {{\n",
            declared.join(", ")
        );
        let line = head.matches('\n').count() + 1;
        let mut args: Vec<String> = case.params.iter().map(Param::value).collect();
        let (wanted, at) = match case.mistake {
            Mistake::Missing => {
                args.pop();
                ("needs an argument", None)
            }
            Mistake::OneMore => {
                args.push("7".into());
                ("one more", Some(args.len() - 1))
            }
            Mistake::Named => {
                args.push("t: 7".into());
                ("has no parameter named", Some(args.len() - 1))
            }
            Mistake::NamedInstead => {
                let last = args.len() - 1;
                args[last] = format!("t: {}", args[last]);
                ("has no parameter named", Some(last))
            }
            Mistake::WrongType(i) => {
                args[i] = "\"x\"".into();
                ("where", Some(i))
            }
        };
        let call = format!("f({})", args.join(", "));
        let source = format!("{head}  r = {call}\n  print(\"{{r}}\")\n}}\n");
        // Where the mistake is: the start of the argument, or the `)`.
        let column = |offset: usize| Pos {
            line,
            col: 1 + "  r = ".len() + offset,
        };
        let place = match at {
            Some(i) => {
                let before: usize = args[..i].iter().map(|a| a.len() + 2).sum();
                column(2 + before)
            }
            None => column(call.len() - 1),
        };
        let errors = match check(source.as_bytes()) {
            Ok(_) => Vec::new(),
            Err(errors) => errors,
        };
        assert_eq!(errors.len(), 1, "{errors:?} for\n{source}");
        let error = &errors[0];
        assert!(error.message.contains(wanted), "{error:?} for\n{source}");
        assert!(!format!("{error:?}").contains('@'), "{error:?} for\n{source}");
        assert_eq!(error.pos, place, "{error:?} for\n{source}");
    });
}
