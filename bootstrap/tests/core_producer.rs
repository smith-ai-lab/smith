//! A function made of whole numbers and `bool` has a core term, and working
//! the term out gives what the program prints. The reference below works a
//! term out with the arithmetic of Rust on 128 bits and shares nothing with
//! the compiler.

use smith::core::{self, Callee, Function, Kind, Lit, Make, Operator, Pattern, Term};
use smith::scalar::{self, Outcome};
use smith::{core_terms, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::numbers::with_header;

pub const ONCE: &[Test] = &[
    (
        "the_core_term_of_a_function_of_numbers_and_bool_gives_what_the_program_prints",
        the_core_term_of_a_function_of_numbers_and_bool_gives_what_the_program_prints,
    ),
    (
        "a_sum_a_difference_and_a_product_are_calls_of_operators_with_the_exact_range",
        a_sum_a_difference_and_a_product_are_calls_of_operators_with_the_exact_range,
    ),
    (
        "a_function_with_anything_else_in_it_has_no_core_term",
        a_function_with_anything_else_in_it_has_no_core_term,
    ),
];

/// A value: a number, or a case of an enum, by its place among the cases.
type Value = i128;

fn work_out(all: &[Function], t: &Term, env: &mut Vec<(String, Value)>) -> Value {
    match &t.kind {
        Kind::Lit(Lit::Int(n)) => *n,
        Kind::Lit(Lit::Text(_)) => panic!("a text"),
        Kind::Name(n) => env.iter().rfind(|(k, _)| k == n).expect("a bound name").1,
        Kind::Make(Make::Case(_, case), _) => *case as i128,
        Kind::Call {
            callee: Callee::Operator(op),
            args,
        } => {
            let (a, b) = (work_out(all, &args[0], env), work_out(all, &args[1], env));
            match op {
                Operator::Add => a + b,
                Operator::Sub => a - b,
                Operator::Mul => a * b,
                Operator::Less => i128::from(a < b),
                Operator::LessEqual => i128::from(a <= b),
                Operator::Greater => i128::from(a > b),
                Operator::GreaterEqual => i128::from(a >= b),
                Operator::Equal => i128::from(a == b),
            }
        }
        Kind::Call {
            callee: Callee::Function(place),
            args,
        } => {
            let f = all.iter().find(|f| f.place == *place).expect("a function with a term");
            let mut inner: Vec<(String, Value)> = f
                .params
                .iter()
                .zip(args)
                .map(|((n, _), a)| (n.clone(), work_out(all, a, env)))
                .collect();
            work_out(all, &f.body, &mut inner)
        }
        Kind::Bind { name, value, rest } => {
            let v = work_out(all, value, env);
            env.push((name.clone(), v));
            let r = work_out(all, rest, env);
            env.pop();
            r
        }
        Kind::Choose { of, arms } => {
            let v = work_out(all, of, env);
            let (_, body) = arms
                .iter()
                .find(|(p, _)| matches!(p, Pattern::Case(c, None) if *c as i128 == v))
                .expect("an arm");
            work_out(all, body, env)
        }
        other => panic!("{other:?}"),
    }
}

fn outcome(source: &str) -> Outcome {
    scalar::run(&lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}")))
}

#[derive(Debug)]
struct Program {
    source: String,
    a: i128,
    b: i128,
}

fn number(g: &mut Gen) -> i128 {
    g.below(21) as i128 - 10
}

fn int_expr(g: &mut Gen, depth: usize, binds: &[&str]) -> String {
    match if depth == 0 { g.below(3) } else { g.below(6) } {
        0 => "a".to_string(),
        1 => "b".to_string(),
        2 => {
            let n = number(g);
            n.to_string()
        }
        3 if !binds.is_empty() => binds[g.below(binds.len())].to_string(),
        _ => format!(
            "if {} {{ {} }} else {{ {} }}",
            bool_expr(g, depth - 1, binds),
            int_expr(g, depth - 1, binds),
            int_expr(g, depth - 1, binds)
        ),
    }
}

fn bool_expr(g: &mut Gen, depth: usize, binds: &[&str]) -> String {
    let op = ["<", "<=", ">", ">=", "==", "!="];
    match if depth == 0 { 0 } else { g.below(4) } {
        0 => format!(
            "{} {} {}",
            int_expr(g, 0, binds),
            op[g.below(6)],
            int_expr(g, 0, binds)
        ),
        1 => format!("({}) and ({})", bool_expr(g, depth - 1, binds), bool_expr(g, depth - 1, binds)),
        2 => format!("({}) or ({})", bool_expr(g, depth - 1, binds), bool_expr(g, depth - 1, binds)),
        _ => format!("not ({})", bool_expr(g, depth - 1, binds)),
    }
}

fn program(g: &mut Gen) -> Program {
    let (a, b) = (number(g), number(g));
    let (bind, binds): (String, &[&str]) = if g.one_in(2) {
        (format!("  t = {}\n", int_expr(g, 1, &[])), &["t"])
    } else {
        (String::new(), &[])
    };
    let body = int_expr(g, 2, binds);
    let helper = if g.one_in(2) {
        format!("fn h(a: i32, b: i32) -> bool {{ {} }}\n\n", bool_expr(g, 1, &[]))
    } else {
        String::new()
    };
    let body = if helper.is_empty() {
        body
    } else {
        format!("if h(a, b) {{ {body} }} else {{ b }}")
    };
    Program {
        source: with_header(format!(
            "{helper}fn f(a: i32, b: i32) -> i32 {{\n{bind}  {body}\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{f({}, {})}}\")\n}}\n",
            a,
            b
        )),
        a,
        b,
    }
}

/// For every function of numbers, comparisons, `and`, `or`, `not`, `if`,
/// bindings, and calls of functions like it, the term is worked out to what
/// the program prints.
fn the_core_term_of_a_function_of_numbers_and_bool_gives_what_the_program_prints() {
    generate::check(120, 200, program, |p| {
        // A program `arith` cannot prove fits is refused, which is not what is
        // tested here.
        let Ok(all) = core_terms(p.source.as_bytes()) else {
            return;
        };
        let f = all.iter().find(|f| f.name == "f").expect("a term for f");
        let mut env = vec![("a".to_string(), p.a), ("b".to_string(), p.b)];
        let got = work_out(&all, &f.body, &mut env);
        let printed = outcome(&p.source);
        assert_eq!(
            printed,
            Outcome {
                stdout: format!("{got}\n").into_bytes(),
                status: 0
            }
        );
    });
}

fn a_sum_a_difference_and_a_product_are_calls_of_operators_with_the_exact_range() {
    let source = with_header(
        "fn f(a: Byte) -> Int\n  requires a < 100\n{\n  a * 2 + 1 - a\n}\n\nfn main() ! console, alloc {\n  print(\"{f(7)}\")\n}\n"
            .to_string(),
    );
    let all = core_terms(source.as_bytes()).expect("checks");
    let f = all.iter().find(|f| f.name == "f").expect("a term for f");
    let shown = f.body.show();
    assert!(
        shown.starts_with("(call - (call + (call * a 2) 1) a)"),
        "{shown}"
    );
    // The product of a byte and 2 holds 0 to 510, whatever the checker keeps it at.
    let Kind::Call { args, .. } = &f.body.kind else {
        panic!("a call")
    };
    let Kind::Call { args: sum, .. } = &args[0].kind else {
        panic!("a call")
    };
    assert_eq!(
        sum[0].ty,
        core::Type::int(0, 510),
        "the range of the product"
    );
}

fn a_function_with_anything_else_in_it_has_no_core_term() {
    let source = with_header(
        "fn word(a: Byte) -> str { \"hi\" }\n\nfn plain(a: Byte) -> Byte { a }\n\nfn main() ! console, alloc {\n  print(word(plain(1)))\n}\n"
            .to_string(),
    );
    let all = core_terms(source.as_bytes()).expect("checks");
    let names: Vec<&str> = all.iter().map(|f| f.name.as_str()).collect();
    assert!(names.contains(&"plain"), "{names:?}");
    assert!(!names.contains(&"word"), "{names:?}");
}
