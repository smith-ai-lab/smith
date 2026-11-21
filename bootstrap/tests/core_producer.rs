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
        "the_core_term_of_a_function_of_structs_tuples_and_enums_gives_what_the_program_prints",
        the_core_term_of_a_function_of_structs_tuples_and_enums_gives_what_the_program_prints,
    ),
    (
        "the_core_term_of_a_function_with_a_list_a_lambda_and_a_call_of_a_function_value_gives_what_the_program_prints",
        the_core_term_of_a_function_with_a_list_a_lambda_and_a_call_of_a_function_value_gives_what_the_program_prints,
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

/// A value: a number, a struct or a tuple with its parts, or a case of an
/// enum with what it holds. `bool` is an enum, so it is a case.
#[derive(Clone, Debug, PartialEq)]
enum Value {
    Int(i128),
    Text(Vec<u8>),
    Parts(Vec<Value>),
    Case(usize, Vec<Value>),
    /// A function written where it is used, with the names it was made among.
    Closure(Vec<String>, Term, Vec<(String, Value)>),
}

fn int(v: &Value) -> i128 {
    match v {
        Value::Int(n) => *n,
        other => panic!("{other:?} is not a number"),
    }
}

fn matches(p: &Pattern, v: &Value, env: &mut Vec<(String, Value)>) -> bool {
    match (p, v) {
        (Pattern::Any, _) => true,
        (Pattern::Bind(n), v) => {
            env.push((n.clone(), v.clone()));
            true
        }
        (Pattern::Lit(Lit::Int(n)), Value::Int(m)) => n == m,
        (Pattern::Lit(Lit::Text(t)), Value::Text(u)) => t == u,
        (Pattern::Case(c, inner), Value::Case(d, held)) => {
            c == d
                && match inner {
                    None => true,
                    Some(p) => matches(p, &held[0], env),
                }
        }
        (Pattern::Tuple(ps), Value::Parts(vs)) => {
            ps.iter().zip(vs).all(|(p, v)| matches(p, v, env))
        }
        _ => false,
    }
}

fn work_out(all: &[Function], t: &Term, env: &mut Vec<(String, Value)>) -> Value {
    let truth = |b: bool| Value::Case(usize::from(b), vec![]);
    match &t.kind {
        Kind::Lit(Lit::Int(n)) => Value::Int(*n),
        Kind::Lit(Lit::Text(text)) => Value::Text(text.clone()),
        Kind::Name(n) => env.iter().rfind(|(k, _)| k == n).expect("a bound name").1.clone(),
        Kind::Make(Make::Case(_, case), args) => {
            Value::Case(*case, args.iter().map(|a| work_out(all, a, env)).collect())
        }
        Kind::Make(Make::Struct(_) | Make::Tuple, args) => {
            Value::Parts(args.iter().map(|a| work_out(all, a, env)).collect())
        }
        Kind::Make(Make::List, items) => {
            Value::Parts(items.iter().map(|a| work_out(all, a, env)).collect())
        }
        Kind::Lambda { params, body } => Value::Closure(
            params.iter().map(|(n, _)| n.clone()).collect(),
            (**body).clone(),
            env.clone(),
        ),
        Kind::Apply { function, args } => {
            let Value::Closure(names, body, mut held) = work_out(all, function, env) else {
                panic!("a function")
            };
            for (n, a) in names.iter().zip(args) {
                let v = work_out(all, a, env);
                held.push((n.clone(), v));
            }
            work_out(all, &body, &mut held)
        }
        Kind::Field { of, index, .. } => match work_out(all, of, env) {
            Value::Parts(parts) => parts[*index].clone(),
            other => panic!("{other:?} has no fields"),
        },
        Kind::Call {
            callee: Callee::Operator(op),
            args,
        } => {
            let (a, b) = (int(&work_out(all, &args[0], env)), int(&work_out(all, &args[1], env)));
            match op {
                Operator::Add => Value::Int(a + b),
                Operator::Sub => Value::Int(a - b),
                Operator::Mul => Value::Int(a * b),
                Operator::Less => truth(a < b),
                Operator::LessEqual => truth(a <= b),
                Operator::Greater => truth(a > b),
                Operator::GreaterEqual => truth(a >= b),
                Operator::Equal => truth(a == b),
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
            for (p, body) in arms {
                let mark = env.len();
                if matches(p, &v, env) {
                    let r = work_out(all, body, env);
                    env.truncate(mark);
                    return r;
                }
                env.truncate(mark);
            }
            panic!("no arm for {v:?}")
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
        let mut env = vec![
            ("a".to_string(), Value::Int(p.a)),
            ("b".to_string(), Value::Int(p.b)),
        ];
        let got = int(&work_out(&all, &f.body, &mut env));
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
        "fn first(xs: List[Byte]) -> u64 { xs.size }\n\nfn plain(a: Byte) -> Byte { a }\n\nfn main() ! console, alloc {\n  print(\"{first([1])}\")\n  print(\"{plain(2)}\")\n}\n"
            .to_string(),
    );
    let all = core_terms(source.as_bytes()).expect("checks");
    let names: Vec<&str> = all.iter().map(|f| f.name.as_str()).collect();
    assert!(names.contains(&"plain"), "{names:?}");
    assert!(!names.contains(&"first"), "{names:?}");
}

#[derive(Debug)]
struct Data {
    source: String,
    k: i128,
    a: i128,
    b: i128,
}

fn data(g: &mut Gen) -> Data {
    let (k, a, b) = (number(g), number(g), number(g));
    let first = ["a", "b", "p.x", "p.y"][g.below(4)];
    let order = ["<", "<=", ">", ">=", "=="][g.below(5)];
    let pick = number(g);
    Data {
        source: with_header(format!(
            "struct P {{\n  x: i32\n  y: i32\n}}\n\n\
enum S {{\n  dot\n  circle: i32\n  rect: P\n}}\n\n\
fn make(k: i32, a: i32, b: i32) -> S {{\n  if k < 0 {{ S.dot }} else if k == 0 {{ .circle(a) }} else {{ .rect(P(x: a, y: b)) }}\n}}\n\n\
fn size(s: S, a: i32, b: i32) -> i32 {{\n  match s {{\n    .dot {{ {pick} }}\n    .circle(r) {{ r }}\n    .rect(p) {{ if p.x {order} p.y {{ {first} }} else {{ p.y }} }}\n  }}\n}}\n\n\
fn pair(a: i32, b: i32) -> i32 {{\n  match (a, b) {{\n    (0, y) {{ y }}\n    (x, _) {{ x }}\n  }}\n}}\n\n\
fn f(k: i32, a: i32, b: i32) -> i32 {{\n  pair(size(make(k, a, b), a, b), a)\n}}\n\n\
fn main() ! console, alloc {{\n  print(\"{{f({k}, {a}, {b})}}\")\n}}\n"
        )),
        k,
        a,
        b,
    }
}

/// For every function of structs, tuples, enums, `match`, and fields, the
/// term is worked out to what the program prints.
fn the_core_term_of_a_function_of_structs_tuples_and_enums_gives_what_the_program_prints() {
    generate::check(121, 150, data, |p| {
        let Ok(all) = core_terms(p.source.as_bytes()) else {
            return;
        };
        let f = all.iter().find(|f| f.name == "f").expect("a term for f");
        let mut env = vec![
            ("k".to_string(), Value::Int(p.k)),
            ("a".to_string(), Value::Int(p.a)),
            ("b".to_string(), Value::Int(p.b)),
        ];
        let got = int(&work_out(&all, &f.body, &mut env));
        assert_eq!(
            outcome(&p.source),
            Outcome {
                stdout: format!("{got}\n").into_bytes(),
                status: 0
            }
        );
    });
}

#[derive(Debug)]
struct Lambdas {
    source: String,
    a: i128,
}

fn lambdas(g: &mut Gen) -> Lambdas {
    let a = number(g);
    let (lo, hi) = (number(g), number(g));
    let (lo, hi) = (lo.min(hi), lo.max(hi));
    let order = ["<", "<=", ">", ">=", "=="][g.below(5)];
    let pivot = number(g);
    Lambdas {
        source: with_header(format!(
            "fn twice(f: fn(i32) -> i32, x: i32) -> i32 {{ f(f(x)) }}\n\n\
fn f(a: i32) -> i32 ! alloc {{\n  xs: List[i32] = [a, {lo}, {hi}]\n  twice(fn(v) {{ if v {order} {pivot} {{ {lo} }} else if v > {hi} {{ {hi} }} else {{ v }} }}, a)\n}}\n\n\
fn main() ! console, alloc {{\n  print(\"{{f({a})}}\")\n}}\n"
        )),
        a,
    }
}

/// For every function that holds a list, writes a lambda, and calls a function
/// that is a value, the term is worked out to what the program prints.
fn the_core_term_of_a_function_with_a_list_a_lambda_and_a_call_of_a_function_value_gives_what_the_program_prints() {
    generate::check(122, 150, lambdas, |p| {
        let Ok(all) = core_terms(p.source.as_bytes()) else {
            return;
        };
        let f = all.iter().find(|f| f.name == "f").expect("a term for f");
        let mut env = vec![("a".to_string(), Value::Int(p.a))];
        let got = int(&work_out(&all, &f.body, &mut env));
        assert_eq!(
            outcome(&p.source),
            Outcome {
                stdout: format!("{got}\n").into_bytes(),
                status: 0
            }
        );
    });
}
