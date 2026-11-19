//! A function with effects is passed by its name, and its effects are
//! required of the function its name is written in, as for a lambda that
//! calls it.

use smith::lower;
use smith::scalar;

use crate::Test;
use crate::generate::{self, Gen};
use crate::programs::pos_after;

pub const ONCE: &[Test] = &[
    (
        "a_function_passed_by_name_builds_the_program_of_the_lambda_that_calls_it",
        a_function_passed_by_name_builds_the_program_of_the_lambda_that_calls_it,
    ),
    (
        "an_effect_the_function_passed_by_name_needs_is_one_error_at_the_name_as_for_its_lambda",
        an_effect_the_function_passed_by_name_needs_is_one_error_at_the_name_as_for_its_lambda,
    ),
    (
        "a_name_inside_a_lambda_asks_the_function_the_lambda_is_written_in_at_any_depth",
        a_name_inside_a_lambda_asks_the_function_the_lambda_is_written_in_at_any_depth,
    ),
];

/// A function `hi` of `parameters` parameters that has these effects, with
/// the line of its body that uses each, and the `do` that receives it.
#[derive(Debug)]
struct Shape {
    parameters: usize,
    console: bool,
    alloc: bool,
}

impl Shape {
    fn generate(g: &mut Gen) -> Shape {
        let effects = 1 + g.below(3);
        Shape {
            parameters: g.below(4),
            console: effects & 1 != 0,
            alloc: effects & 2 != 0,
        }
    }

    /// What `hi` and `do` are, as source.
    fn head(&self) -> String {
        let params: Vec<String> = (0..self.parameters).map(|i| format!("p{i}: u64")).collect();
        let types = vec!["u64"; self.parameters].join(", ");
        let mut effects = Vec::new();
        let mut body = String::new();
        if self.console {
            effects.push("console");
            body.push_str("  print(\"hi\")\n");
        }
        if self.alloc {
            effects.push("alloc");
            body.push_str("  s = \"a{1}\"\n");
        }
        let args = vec!["7"; self.parameters].join(", ");
        let mut uses = String::new();
        if self.parameters > 0 {
            uses.push_str("use std.bits (u64)\n");
        }
        if self.console {
            uses.push_str("use std.console (console, print)\n");
        }
        if !uses.is_empty() {
            uses.push('\n');
        }
        format!(
            "{uses}fn hi({}) ! {} {{\n{body}}}\n\nfn run(f: fn({types})) {{ f({args}) }}\n\n",
            params.join(", "),
            effects.join(", ")
        )
    }

    /// The effects `main` lists to allow `hi`.
    fn listed(&self) -> &'static str {
        match (self.console, self.alloc) {
            (true, true) => "console, alloc",
            (true, false) => "console",
            _ => "alloc",
        }
    }

    /// The lambda that means the name `hi`.
    fn lambda(&self) -> String {
        let names: Vec<String> = (0..self.parameters).map(|i| format!("a{i}")).collect();
        if self.parameters == 0 {
            "{ hi() }".to_string()
        } else {
            format!("fn({}) {{ hi({}) }}", names.join(", "), names.join(", "))
        }
    }
}

fn main_with(shape: &Shape, main_effects: &str, passed: &str) -> String {
    let bang = if main_effects.is_empty() { String::new() } else { format!(" ! {main_effects}") };
    format!("{}fn main(){bang} {{\n  {passed}\n}}\n", shape.head())
}

/// `run(hi)` builds the program that `run fn(..) { hi(..) }` builds, and
/// prints what it does, for any number of parameters and any effects.
fn a_function_passed_by_name_builds_the_program_of_the_lambda_that_calls_it() {
    generate::check(301, 10, Shape::generate, |shape| {
        let listed = shape.listed();
        let by_name = main_with(shape, listed, "run(hi)");
        let by_lambda = main_with(shape, listed, &format!("run {}", shape.lambda()));
        let named = lower(by_name.as_bytes()).unwrap_or_else(|e| panic!("{e:?} in {by_name}"));
        let wrapped = lower(by_lambda.as_bytes()).unwrap_or_else(|e| panic!("{e:?} in {by_lambda}"));
        assert!(named == wrapped, "differs from\n{by_lambda}");
        let out = scalar::run(&named).stdout;
        assert_eq!(out, if shape.console { b"hi\n".to_vec() } else { Vec::new() });
    });
}

/// A function that lacks an effect of the function it passes by name gets an
/// error at the name for each, with the words and the fix of its lambda's.
fn an_effect_the_function_passed_by_name_needs_is_one_error_at_the_name_as_for_its_lambda() {
    generate::check(302, 10, Shape::generate, |shape| {
        let by_name = main_with(shape, "", "run(hi)");
        let found = lower(by_name.as_bytes()).expect_err("an error");
        let before = by_name.strip_suffix("hi)\n}\n").expect("the end");
        let lambda = main_with(shape, "", &format!("run {}", shape.lambda()));
        let wrapped = lower(lambda.as_bytes()).expect_err("an error");
        assert_eq!(found.len(), wrapped.len(), "{found:?}");
        for (e, w) in found.iter().zip(&wrapped) {
            assert_eq!(e.pos, pos_after(before), "{e}");
            assert_eq!((&e.message, &e.fix), (&w.message, &w.fix));
        }
    });
}

/// A name written inside lambdas, at any depth, asks the function the
/// outermost lambda is written in.
fn a_name_inside_a_lambda_asks_the_function_the_lambda_is_written_in_at_any_depth() {
    generate::check(303, 6, |g| 1 + g.below(3), |&depth| {
        let head = "use std.console (console, print)\n\nfn hi() ! console { print(\"hi\") }\n\n\
                    fn run(f: fn()) { f() }\n\n";
        let mut inner = "run(hi)".to_string();
        for _ in 0..depth {
            inner = format!("run {{ {inner} }}");
        }
        let allowed = format!("{head}fn main() ! console {{\n  {inner}\n}}\n");
        let program = lower(allowed.as_bytes()).unwrap_or_else(|e| panic!("{e:?} in {allowed}"));
        assert_eq!(scalar::run(&program).stdout, b"hi\n");
        let refused = format!("{head}fn main() {{\n  {inner}\n}}\n");
        let found = lower(refused.as_bytes()).expect_err("an error");
        assert_eq!(found.len(), 1, "{found:?}");
        let error = &found[0];
        let at = refused.rfind("(hi)").expect("the name") + 1;
        assert_eq!(error.pos, pos_after(&refused[..at]), "{error}");
        assert!(error.message.contains("console") && error.message.contains("main"), "{error}");
    });
}
