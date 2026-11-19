//! The pipeline `x |> f(a)` is the call `f(x, a)`, and nothing after the
//! parser knows of it.
//!
//! Each generator builds a pipeline and the call it stands for from the same
//! choices, so the model of each is the other.

use smith::{check, compile};
use smith::target::Target;

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "a_pipeline_builds_to_the_bytes_of_the_call_it_stands_for_on_every_target",
        a_pipeline_builds_to_the_bytes_of_the_call_it_stands_for_on_every_target,
    ),
    (
        "a_line_that_starts_with_a_pipe_goes_on_from_the_line_before_at_any_depth",
        a_line_that_starts_with_a_pipe_goes_on_from_the_line_before_at_any_depth,
    ),
    (
        "a_pipeline_binds_more_loosely_than_every_operator_and_groups_from_the_left",
        a_pipeline_binds_more_loosely_than_every_operator_and_groups_from_the_left,
    ),
    (
        "the_right_side_is_a_call_of_a_name_a_module_a_field_or_one_that_ends_with_a_lambda",
        the_right_side_is_a_call_of_a_name_a_module_a_field_or_one_that_ends_with_a_lambda,
    ),
    (
        "try_is_written_on_the_step_it_belongs_to",
        try_is_written_on_the_step_it_belongs_to,
    ),
    (
        "a_mistake_in_a_pipeline_is_one_error_at_the_place_it_is_in_the_call_it_stands_for",
        a_mistake_in_a_pipeline_is_one_error_at_the_place_it_is_in_the_call_it_stands_for,
    ),
    (
        "two_holes_a_hole_inside_an_argument_and_a_right_side_that_is_no_call_are_each_one_error",
        two_holes_a_hole_inside_an_argument_and_a_right_side_that_is_no_call_are_each_one_error,
    ),
];

/// What every program here imports: the `main` of each prints.
const USES: &str = "use std.console (console, print)\n\n";

/// Three functions of three whole numbers each, which use their parameters in
/// different ways, so that a value in another place gives other code.
const FUNCTIONS: &str = "\
fn f0(a: Byte, b: Byte, c: Byte) -> Byte { if a < b { c } else { a } }
fn f1(a: Byte, b: Byte, c: Byte) -> Byte { if b < c { a } else { b } }
fn f2(a: Byte, b: Byte, c: Byte) -> Byte { if c < a { b } else { c } }
";

/// A step of a pipeline: a function, the place the value goes to, and how
/// that place is written.
#[derive(Debug)]
struct Step {
    function: usize,
    place: usize,
    /// 0 leaves `_` out where the place is the first, 1 writes it, and 2
    /// writes it with the name of the parameter, where the place is the
    /// last.
    form: usize,
    others: [u8; 2],
}

fn step(g: &mut Gen) -> Step {
    let function = g.below(3);
    let place = g.below(3);
    let form = match place {
        0 => g.below(2),
        2 => 1 + g.below(2),
        _ => 1,
    };
    Step {
        function,
        place,
        form,
        others: [g.below(200) as u8, g.below(200) as u8],
    }
}

impl Step {
    /// The arguments, with `value` in the place of the value.
    fn arguments(&self, value: &str, named: bool) -> String {
        let mut others = self.others.iter();
        let mut args = Vec::new();
        for at in 0..3 {
            args.push(if at == self.place {
                if named && self.form == 2 {
                    format!("{}: {value}", ["a", "b", "c"][at])
                } else {
                    value.to_string()
                }
            } else {
                others.next().expect("two others").to_string()
            });
        }
        if !named && self.place == 0 && self.form == 0 {
            args.remove(0);
        }
        args.join(", ")
    }

    /// The step as it follows a `|>`.
    fn piped(&self) -> String {
        let value = if self.form == 0 { "" } else { "_" };
        let named = self.form == 2;
        let args = self.arguments(value, named);
        format!("f{}({args})", self.function)
    }

    /// The call the step stands for, with `value` as its value.
    fn call(&self, value: &str) -> String {
        format!("f{}({})", self.function, self.arguments(value, true))
    }
}

fn program(expr: &str) -> String {
    format!("{USES}{FUNCTIONS}fn main() ! console, alloc {{\n  print(\"{{{expr}}}\")\n}}\n")
}

fn bytes(source: &str, target: Target) -> Vec<u8> {
    compile(source.as_bytes(), target).unwrap_or_else(|e| panic!("{e:?} for {source}"))
}

/// The pipeline written on one line, and the call it stands for.
fn forms(first: u8, steps: &[Step]) -> (String, String) {
    let mut written = first.to_string();
    let mut piped = first.to_string();
    for s in steps {
        written = s.call(&written);
        piped = format!("{piped} |> {}", s.piped());
    }
    (piped, written)
}

fn a_pipeline_builds_to_the_bytes_of_the_call_it_stands_for_on_every_target() {
    generate::check(
        470_001,
        6,
        |g| {
            let first = g.below(200) as u8;
            let count = 1 + g.below(3);
            (first, (0..count).map(|_| step(g)).collect::<Vec<_>>())
        },
        |(first, steps)| {
            let (piped, written) = forms(*first, steps);
            for &target in Target::ALL {
                assert_eq!(
                    bytes(&program(&piped), target),
                    bytes(&program(&written), target),
                    "{piped} is not {written} on {}",
                    target.name()
                );
            }
        },
    );
}

fn a_line_that_starts_with_a_pipe_goes_on_from_the_line_before_at_any_depth() {
    generate::check(
        470_002,
        10,
        |g| {
            let count = 2 + g.below(3);
            let steps: Vec<Step> = (0..count).map(|_| step(g)).collect();
            // How far in each line is, and what stands before it: nothing,
            // an empty line, or a comment.
            let lines: Vec<(usize, usize)> = (0..count).map(|_| (g.below(5), g.below(3))).collect();
            (g.below(200) as u8, steps, lines, g.below(3))
        },
        |(first, steps, lines, depth)| {
            let (_, written) = forms(*first, steps);
            let indent = |n: usize| " ".repeat(n * 2);
            let inside = indent(1 + depth);
            let mut piped = format!("{first}\n");
            for (s, (extra, before)) in steps.iter().zip(lines) {
                piped.push_str(&match before {
                    1 => "\n".to_string(),
                    2 => format!("{inside}// a comment\n"),
                    _ => String::new(),
                });
                piped.push_str(&format!("{}|> {}\n", indent(1 + depth + extra), s.piped()));
            }
            // The value is bound inside `if`s `depth` deep.
            let nested = |value: &str| {
                let mut body = String::from("  var r: Byte = 0\n");
                for d in 0..*depth {
                    body.push_str(&format!("{}if r == 0 {{\n", indent(1 + d)));
                }
                body.push_str(&format!("{inside}r = {value}\n"));
                for d in (0..*depth).rev() {
                    body.push_str(&format!("{}}}\n", indent(1 + d)));
                }
                format!("{USES}{FUNCTIONS}fn main() ! console, alloc {{\n{body}  print(\"{{r}}\")\n}}\n")
            };
            let (lines, single) = (nested(piped.trim_end()), nested(&written));
            for &target in Target::ALL {
                assert_eq!(bytes(&lines, target), bytes(&single, target), "for {lines}");
            }
        },
    );
}

fn a_pipeline_binds_more_loosely_than_every_operator_and_groups_from_the_left() {
    // `a < b |> g(c)` is `g(a < b, c)`, so a whole expression stands on each
    // side, and `x |> f() |> g()` is `g(f(x))`.
    let source = |expr: &str| {
        format!(
            "use std.console (console, print)\n\n\
             fn g(a: bool, b: Byte) -> Byte {{ if a {{ b }} else {{ 0 }} }}\n\
             fn h(a: bool) -> Byte {{ if a {{ 1 }} else {{ 2 }} }}\n\
             fn n(a: Byte) -> bool {{ a < 3 }}\n\
             fn main() ! console, alloc {{\n  print(\"{{{expr}}}\")\n}}\n"
        )
    };
    for (piped, written) in [
        ("1 < 2 |> g(7)", "g(1 < 2, 7)"),
        ("1 < 2 and 2 < 3 |> g(7)", "g(1 < 2 and 2 < 3, 7)"),
        ("1 < 2 or 4 < 3 |> g(7)", "g(1 < 2 or 4 < 3, 7)"),
        ("not 1 < 2 |> g(7)", "g(not 1 < 2, 7)"),
        ("1 |> n() |> h()", "h(n(1))"),
        ("1 |> n() |> g(5)", "g(n(1), 5)"),
    ] {
        for &target in Target::ALL {
            assert_eq!(
                bytes(&source(piped), target),
                bytes(&source(written), target),
                "{piped} is not {written}"
            );
        }
    }
}

fn the_right_side_is_a_call_of_a_name_a_module_a_field_or_one_that_ends_with_a_lambda() {
    let source = |expr: &str| {
        format!(
            "use std.ascii\nuse std.console (console, print)\n\n\
             fn same(x: Byte) -> Byte {{ x }}\n\
             fn apply(x: Byte, f: fn(Byte) -> Byte) -> Byte {{ f(x) }}\n\
             fn main() ! console, alloc {{\n  print(\"{{{expr}}}\")\n}}\n"
        )
    };
    for (piped, written) in [
        ("65 |> same()", "same(65)"),
        ("65 |> ascii.is_digit()", "ascii.is_digit(65)"),
        ("5 |> apply() fn(v) { v }", "apply(5) fn(v) { v }"),
        ("5 |> apply(fn(v) { v })", "apply(5, fn(v) { v })"),
    ] {
        for &target in Target::ALL {
            assert_eq!(
                bytes(&source(piped), target),
                bytes(&source(written), target),
                "{piped} is not {written}"
            );
        }
    }
    // A function stored in a field is not built yet, so a call of one is
    // refused, and the pipeline is refused as the call it stands for is.
    let field = |call: &str| {
        format!(
            "use std.console (console, print)\n\n\
             struct Pricing {{\n  discount: fn(Byte) -> Byte\n}}\n\n\
             fn same(x: Byte) -> Byte {{ x }}\n\
             fn main() ! console, alloc {{\n  sale = Pricing(discount: same)\n  print(\"{{{call}}}\")\n}}\n"
        )
    };
    let said = |call: &str| {
        let errors = check(field(call).as_bytes()).expect_err("refused");
        errors.iter().map(|e| e.message.clone()).collect::<Vec<_>>()
    };
    assert_eq!(said("65 |> sale.discount()"), said("sale.discount(65)"));
}

fn try_is_written_on_the_step_it_belongs_to() {
    let source = |body: &str| {
        format!(
            "use std.console (console, print)\n\n\
             fn parse(t: Byte) -> Result[Byte, Byte] {{ .ok(t) }}\n\
             fn scale(p: Byte, by: Byte) -> Byte {{ if p < by {{ p }} else {{ by }} }}\n\
             fn check(p: Byte) -> Result[Byte, Byte] {{ .ok(p) }}\n\
             fn read(t: Byte) -> Result[Byte, Byte] {{\n{body}  .ok(point)\n}}\n\
             fn main() ! console, alloc {{\n  match read(4) {{\n    .ok(v) {{ print(\"{{v}}\") }}\n    .err(e) {{ print(\"{{e}}\") }}\n  }}\n}}\n"
        )
    };
    for (piped, written) in [
        (
            "t |> try parse() |> scale(2) |> try check()",
            "try check(scale(try parse(t), 2))",
        ),
        ("t |> try parse(_)", "try parse(t)"),
        // Before the first value, `try` covers that value alone.
        ("try parse(t) |> scale(2)", "scale(try parse(t), 2)"),
    ] {
        let piped = source(&format!("  point = {piped}\n"));
        let written = source(&format!("  point = {written}\n"));
        for &target in Target::ALL {
            assert_eq!(bytes(&piped, target), bytes(&written, target), "for {piped}");
        }
    }
}

/// Where the place marked with `\u{1}` is, and the text without the mark.
fn marked(source: &str) -> (String, (usize, usize)) {
    let at = source.find('\u{1}').expect("a mark");
    let before = &source[..at];
    let place = (before.matches('\n').count() + 1, at - before.rfind('\n').map_or(0, |n| n + 1) + 1);
    (source.replace('\u{1}', ""), place)
}

/// The places of the errors the source gives, and what they say, joined.
fn mistakes(source: &str) -> (Vec<(usize, usize)>, String) {
    let errors = check(source.as_bytes()).expect_err(&format!("compiled: {source}"));
    let places = errors.iter().map(|e| (e.pos.line, e.pos.col)).collect();
    let said = errors.iter().map(|e| e.message.clone()).collect::<Vec<_>>().join("\n");
    (places, said)
}

fn a_mistake_in_a_pipeline_is_one_error_at_the_place_it_is_in_the_call_it_stands_for() {
    const PRELUDE: &str = "\
fn take_it(x: take List[Byte], n: Byte) -> List[Byte] { x }
fn say(x: Byte) -> Byte ! console { x }
fn pick(xs: List[Byte], i: Nat) -> Byte requires i < xs.size { xs[i] }
fn pair(a: Byte, b: Byte) -> Byte { a }
";
    generate::check(
        470_003,
        12,
        |g| (g.below(5), g.below(200), g.below(200)),
        |&(kind, x, y)| {
            // The pipeline with the place of the error marked, the name the
            // error must mention, and the call the pipeline stands for.
            let (piped, name, written) = match kind {
                0 => (
                    format!("fn f() -> Byte {{ \u{1}\"s\" |> pair({x}) }}"),
                    "Byte",
                    format!("fn f() -> Byte {{ pair(\"s\", {x}) }}"),
                ),
                1 => (
                    format!("fn f() -> Byte {{ {x} |> pair(\u{1}\"s\") }}"),
                    "Byte",
                    format!("fn f() -> Byte {{ pair({x}, \"s\") }}"),
                ),
                2 => (
                    format!("fn f() -> Byte {{ {y} |> pair(\u{1}\"s\", _) }}"),
                    "Byte",
                    format!("fn f() -> Byte {{ pair(\"s\", {y}) }}"),
                ),
                3 => (
                    format!("fn f() -> Byte {{ {x} |> \u{1}say() }}"),
                    "console",
                    format!("fn f() -> Byte {{ say({x}) }}"),
                ),
                _ => (
                    "fn f(xs: List[Byte], i: Nat) -> Byte { i |> \u{1}pick(xs, _) }".to_string(),
                    "pick",
                    "fn f(xs: List[Byte], i: Nat) -> Byte { pick(xs, i) }".to_string(),
                ),
            };
            let main = "fn main() ! console, alloc {\n  xs: List[Byte] = []\n  print(\"{f(xs, 1)}\")\n}\n";
            let main = if kind == 4 { main } else { &main.replace("f(xs, 1)", "f()") };
            let wrap = |f: &str| format!("{USES}{PRELUDE}{f}\n{main}");
            let (source, place) = marked(&wrap(&piped));
            let (places, said) = mistakes(&source);
            assert_eq!(places, vec![place], "{said} for {source}");
            assert!(said.contains(name), "{said} for {source}");
            let (written_places, _) = mistakes(&wrap(&written));
            assert_eq!(written_places.len(), 1, "for {written}");
        },
    );
    // A value used after it was given away is the second use.
    let (source, place) = marked(&format!(
        "{USES}{PRELUDE}fn main() ! console, alloc {{\n  xs: List[Byte] = []\n  a = xs |> take_it(1)\n  b = \u{1}xs |> take_it(2)\n  print(\"{{a.size}}{{b.size}}\")\n}}\n"
    ));
    let (places, said) = mistakes(&source);
    assert_eq!(places, vec![place], "{said}");
    assert!(said.contains("xs"), "{said}");
}

fn two_holes_a_hole_inside_an_argument_and_a_right_side_that_is_no_call_are_each_one_error() {
    const PRELUDE: &str = "fn pair(a: Byte, b: Byte) -> Byte { a }\nfn one(a: Byte) -> Byte { a }\n";
    for (piped, word) in [
        // The second `_`.
        ("pair(_, \u{1}_)", "one `_`"),
        ("pair(_, to: \u{1}_)", "one `_`"),
        // A `_` that is not a whole argument.
        ("pair(1, one(\u{1}_))", "inside"),
        ("pair(1, \u{1}_ < 2)", "inside"),
        // A right side that is no call.
        ("\u{1}pair", "not a call"),
        ("\u{1}1", "not a call"),
        ("\u{1}one(1).x", "not a call"),
    ] {
        let source = format!(
            "{USES}{PRELUDE}fn f(x: Byte) -> Byte {{ x |> {piped} }}\nfn main() ! console, alloc {{ print(\"{{f(1)}}\") }}\n"
        );
        let (source, place) = marked(&source);
        let errors = check(source.as_bytes()).expect_err(&format!("compiled: {source}"));
        let pipeline: Vec<_> = errors.iter().filter(|e| e.message.contains(word)).collect();
        assert_eq!(pipeline.len(), 1, "{errors:?} for {source}");
        assert_eq!((pipeline[0].pos.line, pipeline[0].pos.col), place, "for {source}");
        assert!(pipeline[0].fix.is_some(), "no fix for {source}");
    }
}
