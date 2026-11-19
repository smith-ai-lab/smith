//! Generators and properties of a plain `+`, `-`, or `*` on whole numbers,
//! which compiles only when `arith` proves that its result fits the type of
//! its operands.
//!
//! Every generator works out the answer with the arithmetic of Rust on 128
//! bits, or by trying every value of the variables, so it shares nothing with
//! the compiler.

use smith::scalar::{self, Outcome};
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::numbers::{INTS, Int, value, with_header};

pub const ONCE: &[Test] = &[
    (
        "an_operation_on_two_numbers_compiles_exactly_when_its_true_result_fits_and_prints_it",
        an_operation_on_two_numbers_compiles_exactly_when_its_true_result_fits_and_prints_it,
    ),
    (
        "an_operation_inside_the_conditions_that_bound_its_operands_compiles_exactly_when_it_fits_and_never_wraps",
        an_operation_inside_the_conditions_that_bound_its_operands_compiles_exactly_when_it_fits_and_never_wraps,
    ),
    (
        "arith_says_yes_exactly_when_no_values_make_the_facts_true_and_the_goal_false",
        arith_says_yes_exactly_when_no_values_make_the_facts_true_and_the_goal_false,
    ),
    (
        "each_operation_of_an_expression_is_proven_by_itself",
        each_operation_of_an_expression_is_proven_by_itself,
    ),
    (
        "a_product_of_two_values_that_are_not_constants_is_refused",
        a_product_of_two_values_that_are_not_constants_is_refused,
    ),
    (
        "an_accepted_operation_is_the_one_instruction_and_tests_nothing",
        an_accepted_operation_is_the_one_instruction_and_tests_nothing,
    ),
    (
        "operators_group_as_the_precedence_table_says",
        operators_group_as_the_precedence_table_says,
    ),
];

const OPS: [&str; 3] = ["+", "-", "*"];

/// What the operation gives for the values, or none when it gives more than
/// 128 bits hold, which is more than any type does.
fn exact(op: &str, a: i128, b: i128) -> Option<i128> {
    match op {
        "+" => a.checked_add(b),
        "-" => a.checked_sub(b),
        _ => a.checked_mul(b),
    }
}

fn fits(ty: Int, n: Option<i128>) -> bool {
    n.is_some_and(|n| (ty.min()..=ty.max()).contains(&n))
}

/// Whether the exact range of `op` on two numbers of the type is one that a
/// type of 64 bits holds, so that the operation gives it with nothing proven,
/// whatever the numbers are.
fn always_fits(ty: Int, op: &str) -> bool {
    let ends = [ty.min(), ty.max()];
    let results: Vec<Option<i128>> = ends
        .iter()
        .flat_map(|a| ends.iter().map(|b| exact(op, *a, *b)))
        .collect();
    let (Some(low), Some(high)) = (
        results.iter().copied().flatten().min(),
        results.iter().copied().flatten().max(),
    ) else {
        return false;
    };
    results.iter().all(Option::is_some)
        && if low < 0 {
            low >= i64::MIN as i128 && high <= i64::MAX as i128
        } else {
            high <= u64::MAX as i128
        }
}

/// A value of the type next to one of its limits, or next to the value that
/// makes `op` with `other` give a limit.
fn near(g: &mut Gen, ty: Int, op: &str, other: i128) -> i128 {
    let d = g.below(3) as i128 - 1;
    let limit = if g.one_in(2) { ty.max() } else { ty.min() };
    let aimed = match op {
        "+" => limit - other,
        "-" => other - limit,
        _ if other != 0 => limit / other,
        _ => limit,
    };
    (aimed + d).clamp(ty.min(), ty.max())
}

/// How a number is written as a literal.
fn lit(n: i128) -> String {
    n.to_string()
}

fn run(source: &str) -> Outcome {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    scalar::run(&program)
}

#[derive(Debug)]
struct Pair {
    ty: Int,
    op: &'static str,
    a: i128,
    b: i128,
}

fn pair(g: &mut Gen) -> Pair {
    let ty = *g.pick(INTS);
    let op = *g.pick(&OPS);
    let a = value(g, ty);
    let b = if g.one_in(2) {
        near(g, ty, op, a)
    } else {
        value(g, ty)
    };
    Pair { ty, op, a, b }
}

/// For every integer type and every pair of numbers of it, `a + b`, `a - b`,
/// and `a * b` each compile when the exact range of the result is one that a
/// type of 64 bits holds, and otherwise exactly when the true result is a
/// value of the type, and the program prints the true result.
fn an_operation_on_two_numbers_compiles_exactly_when_its_true_result_fits_and_prints_it() {
    generate::check(90, 150, pair, |p| {
        let source = with_header(format!(
            "fn main() ! console, alloc {{\n  a: {0} = {1}\n  b: {0} = {2}\n  print(\"{{a {3} b}}\")\n}}\n",
            p.ty.name,
            lit(p.a),
            lit(p.b),
            p.op
        ));
        let result = exact(p.op, p.a, p.b);
        match check(source.as_bytes()) {
            Ok(_) => {
                assert!(
                    always_fits(p.ty, p.op) || fits(p.ty, result),
                    "accepted, though {result:?} does not fit"
                );
                assert_eq!(
                    run(&source),
                    Outcome {
                        stdout: format!("{}\n", result.unwrap()).into_bytes(),
                        status: 0
                    }
                );
            }
            Err(errors) => {
                assert!(
                    !always_fits(p.ty, p.op) && !fits(p.ty, result),
                    "refused, though {result:?} fits"
                );
                let goal = format!("a {} b", p.op);
                assert!(
                    errors.iter().any(|e| e.message.contains(&goal)),
                    "{errors:?} does not state the goal about `{goal}`"
                );
            }
        }
    });
}

/// How a bound of a variable is written, and how it is written as the
/// condition that it does not hold.
fn bound(g: &mut Gen, x: &str, upper: bool, at: i128, ty: Int) -> (String, String) {
    let (held, broken) = if upper {
        match g.below(4) {
            0 => (format!("{x} <= {at}"), format!("{x} > {at}")),
            1 => (format!("{at} >= {x}"), format!("{at} < {x}")),
            2 => (format!("not ({x} > {at})"), format!("not ({x} <= {at})")),
            _ if at < ty.max() => (format!("{x} < {}", at + 1), format!("{x} >= {}", at + 1)),
            _ => (format!("{x} <= {at}"), format!("{x} > {at}")),
        }
    } else {
        match g.below(4) {
            0 => (format!("{x} >= {at}"), format!("{x} < {at}")),
            1 => (format!("{at} <= {x}"), format!("{at} > {x}")),
            2 => (format!("not ({x} < {at})"), format!("not ({x} >= {at})")),
            _ if at > ty.min() => (format!("{x} > {}", at - 1), format!("{x} <= {}", at - 1)),
            _ => (format!("{x} >= {at}"), format!("{x} < {at}")),
        }
    };
    (held, broken)
}

#[derive(Debug)]
struct Boxed {
    source: String,
    /// Whether the true result fits for every value inside the bounds.
    fits: bool,
    /// What the operation is written as, for the error.
    written: String,
    /// What `main` prints.
    stdout: String,
}

/// A function of two variables of one type whose operation is inside
/// conditions that bound them, written in one of several forms, and a `main`
/// that calls it with values at and next to the bounds.
fn boxed(g: &mut Gen) -> Boxed {
    let ty = *g.pick(INTS);
    let op = *g.pick(&OPS);
    let (lx, hx) = ordered(value(g, ty), value(g, ty));
    // The other variable's bounds are aimed so that the result is at a limit
    // or next to it.
    let aim = |g: &mut Gen, at: i128| {
        if g.one_in(2) {
            near(g, ty, op, at)
        } else {
            value(g, ty)
        }
    };
    let (ly, hy) = ordered(aim(g, hx), aim(g, lx));
    // A product is of a variable and a number, which is written on either
    // side.
    let c = if op == "*" {
        let c = if ty.signed {
            g.below(7) as i128 - 3
        } else {
            g.below(4) as i128
        };
        if c == 0 { 1 } else { c }
    } else {
        0
    };
    let c_left = g.one_in(2);
    let (operation, results): (String, Vec<i128>) = match op {
        "*" if c_left => (format!("{c} * x"), vec![c * lx, c * hx]),
        "*" => (format!("x * {c}"), vec![c * lx, c * hx]),
        "+" => ("x + y".into(), vec![lx + ly, hx + hy]),
        _ => ("x - y".into(), vec![lx - hy, hx - ly]),
    };
    let fits_all = results.iter().all(|r| fits(ty, Some(*r)));
    let mut held = Vec::new();
    let mut broken = Vec::new();
    for (name, lo, hi) in [("x", lx, hx), ("y", ly, hy)] {
        if op == "*" && name == "y" {
            continue;
        }
        for (upper, at) in [(false, lo), (true, hi)] {
            let (h, b) = bound(g, name, upper, at, ty);
            held.push(h);
            broken.push(b);
        }
    }
    let t = ty.name;
    let body = match g.below(3) {
        0 => format!(
            "  if {} {{\n    {operation}\n  }} else {{\n    0\n  }}\n",
            held.join(" and ")
        ),
        1 => format!(
            "  if {} {{\n    0\n  }} else {{\n    {operation}\n  }}\n",
            broken.join(" or ")
        ),
        _ => {
            let mut text = String::new();
            for h in &held {
                text.push_str(&format!("  if {h} {{\n"));
            }
            text.push_str(&format!("    {operation}\n"));
            for _ in &held {
                text.push_str("  } else {\n    0\n  }\n");
            }
            text
        }
    };
    let source_fn = format!("fn f(x: {t}, y: {t}) -> {t} {{\n{body}}}\n");
    let around = |lo: i128, hi: i128| {
        let mut values = vec![lo, hi];
        for d in [-1, 1] {
            values.extend([lo + d, hi + d]);
        }
        values.retain(|v| (ty.min()..=ty.max()).contains(v));
        values.dedup();
        values
    };
    let mut calls = String::new();
    let mut stdout = String::new();
    for x in around(lx, hx) {
        for y in around(ly, hy) {
            calls.push_str(&format!("  print(\"{{f({x}, {y})}}\")\n"));
            let inside = (lx..=hx).contains(&x) && (op == "*" || (ly..=hy).contains(&y));
            let answer = if !inside {
                0
            } else if op == "*" {
                c * x
            } else {
                exact(op, x, y).expect("small")
            };
            stdout.push_str(&format!("{answer}\n"));
        }
    }
    Boxed {
        source: with_header(format!(
            "{source_fn}\nfn main() ! console, alloc {{\n{calls}}}\n"
        )),
        fits: fits_all,
        written: operation,
        stdout,
    }
}

fn ordered(a: i128, b: i128) -> (i128, i128) {
    (a.min(b), a.max(b))
}

/// For every operation inside the conditions that bound its operands, in
/// every form a condition is written in, the program compiles exactly when
/// the true result fits for every value inside the bounds, and then, at and
/// next to the bounds, the operation gives the true result and never wraps.
fn an_operation_inside_the_conditions_that_bound_its_operands_compiles_exactly_when_it_fits_and_never_wraps()
 {
    generate::check(91, 150, boxed, |b| match check(b.source.as_bytes()) {
        Ok(_) => {
            assert!(b.fits, "accepted, though a result is out of range");
            assert_eq!(
                run(&b.source),
                Outcome {
                    stdout: b.stdout.clone().into_bytes(),
                    status: 0
                }
            );
        }
        Err(errors) => {
            assert!(!b.fits, "refused, though every result fits: {errors:?}");
            assert!(
                errors.iter().any(|e| e.message.contains(&b.written)),
                "{errors:?} does not state the goal about `{}`",
                b.written
            );
        }
    });
}

/// A sum `a * x + b * y + k` of the variables, written out.
fn linear(a: i128, b: i128, k: i128) -> String {
    let mut text = match (a, b) {
        (0, 0) => return lit(k),
        (0, b) => format!("{b} * y"),
        (a, 0) => format!("{a} * x"),
        (a, b) => format!("{a} * x + {b} * y"),
    };
    if k != 0 {
        text.push_str(&format!(" + {k}"));
    }
    text
}

/// A condition on the variables, written out, with the function that says
/// for which values it holds.
type Condition = (String, Box<dyn Fn(i128, i128) -> bool>);

fn condition(g: &mut Gen, depth: usize, big: bool) -> Condition {
    if depth > 0 && g.one_in(2) {
        let (a, f) = condition(g, depth - 1, big);
        return match g.below(3) {
            0 => (format!("not ({a})"), Box::new(move |x, y| !f(x, y))),
            joined => {
                let (b, h) = condition(g, depth - 1, big);
                if joined == 1 {
                    (
                        format!("({a}) and ({b})"),
                        Box::new(move |x, y| f(x, y) && h(x, y)),
                    )
                } else {
                    (
                        format!("({a}) or ({b})"),
                        Box::new(move |x, y| f(x, y) || h(x, y)),
                    )
                }
            }
        };
    }
    // Larger numbers make the equations and bounds that need the most care,
    // when the type has room for the operations on them.
    let (reach, ks) = if big { (19, 31) } else { (9, 9) };
    // A thin slab between two lines, with numbers that have little in common,
    // has whole numbers inside it only now and then.
    if big && g.one_in(3) {
        let (a, b) = (g.below(27) as i128 - 13, g.below(27) as i128 - 13);
        let (lo, width) = (g.below(61) as i128 - 30, g.below(30) as i128);
        let hi = lo + width;
        return (
            format!("({lo} <= {sum}) and ({sum} <= {hi})", sum = linear(a, b, 0)),
            Box::new(move |x, y| {
                let v = a * x + b * y;
                (lo..=hi).contains(&v)
            }),
        );
    }
    let small = |g: &mut Gen| g.below(reach) as i128 - reach as i128 / 2;
    let constant = |g: &mut Gen| g.below(ks) as i128 - ks as i128 / 2;
    let left = (small(g), small(g), constant(g));
    let right = (small(g), small(g), constant(g));
    let op = *g.pick(&["<", "<=", ">", ">=", "==", "!="]);
    let text = format!(
        "{} {op} {}",
        linear(left.0, left.1, left.2),
        linear(right.0, right.1, right.2)
    );
    (
        text,
        Box::new(move |x, y| {
            let l = left.0 * x + left.1 * y + left.2;
            let r = right.0 * x + right.1 * y + right.2;
            crate::numbers::compare(op, l, r)
        }),
    )
}

struct Facts {
    source: String,
    /// Whether every operation fits whenever the facts hold.
    fits: bool,
}

impl std::fmt::Debug for Facts {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.source)
    }
}

/// A function of two variables that lie between -8 and 8, whose goals are
/// the operations of a sum of the variables, each times a number, and a
/// number, inside the nested `if`s of conditions that are linear in the
/// variables, each with the `then` or the `else` taking the sum.
fn facts_and_goal(g: &mut Gen) -> Facts {
    let signed: Vec<Int> = INTS.iter().filter(|t| t.signed).copied().collect();
    let ty = *g.pick(&signed);
    // The sum's numbers are such that it goes past the limits of the type for
    // some values of the variables.
    let scale = (ty.max() / 16).max(1);
    let (a, b) = (
        (g.below(7) as i128 - 3) * scale,
        (g.below(7) as i128 - 3) * scale,
    );
    let k = *g.pick(&[0, 1, -1, ty.max() / 2, -(ty.max() / 2), ty.max() / 4]);
    let mut conds: Vec<(Condition, bool)> = Vec::new();
    for _ in 0..1 + g.below(3) {
        conds.push((condition(g, 1, ty.bits >= 16), g.one_in(2)));
    }
    let holds = |x: i128, y: i128| conds.iter().all(|(c, then)| c.1(x, y) == *then);
    let sum = linear(a, b, k);
    let mut body = String::from("  if x >= -8 and x <= 8 and y >= -8 and y <= 8 {\n");
    for (c, then) in &conds {
        if *then {
            body.push_str(&format!("  if {} {{\n", c.0));
        } else {
            body.push_str(&format!("  if {} {{\n    0\n  }} else {{\n", c.0));
        }
    }
    body.push_str(&format!("    {sum}\n"));
    for (_, then) in conds.iter().rev() {
        body.push_str(if *then {
            "  } else {\n    0\n  }\n"
        } else {
            "  }\n"
        });
    }
    body.push_str("  } else {\n    0\n  }\n");
    let t = ty.name;
    let source = with_header(format!(
        "fn f(x: {t}, y: {t}) -> {t} {{\n{body}}}\n\nfn main() ! console, alloc {{\n  print(\"{{f(1, 2)}}\")\n}}\n"
    ));
    let mut fits_all = true;
    for x in -8..=8 {
        for y in -8..=8 {
            if holds(x, y) {
                // The operations of `a * x + b * y + k` that are written. Each
                // must fit where its exact range is not one a type holds, as
                // for 64 bits. Where it is, only the whole, which is given as
                // the result, must fit.
                let parts = [a * x, b * y, a * x + b * y, a * x + b * y + k];
                let written = [a != 0, b != 0, a != 0 && b != 0, k != 0];
                if ty.bits == 64 {
                    for (part, written) in parts.iter().zip(written) {
                        if written && !(ty.min()..=ty.max()).contains(part) {
                            fits_all = false;
                        }
                    }
                } else if !(ty.min()..=ty.max()).contains(&(a * x + b * y + k)) {
                    fits_all = false;
                }
            }
        }
    }
    Facts {
        source,
        fits: fits_all,
    }
}

/// For every set of facts and every goal in linear arithmetic over two
/// variables with small ranges, `arith` says yes exactly when no values of
/// the variables make the facts true and the goal false. The test finds the
/// right answer by trying every value, and the types range up to 64 bits, so
/// an answer that took time in proportion to a range would not come.
fn arith_says_yes_exactly_when_no_values_make_the_facts_true_and_the_goal_false() {
    generate::check(92, 200, facts_and_goal, |f| {
        let accepted = check(f.source.as_bytes()).is_ok();
        assert_eq!(accepted, f.fits, "for\n{}", f.source);
    });
}

/// In `a + b - c` of numbers of a type of 64 bits, `a + b` must fit even when
/// the whole would, and so for `a - b + c`, as the exact range of the result of
/// an operation on two of them is not one a type holds. Of a smaller type, it
/// is, so the operations give the exact range and nothing is proven, and the
/// whole is what must fit where it is given.
fn each_operation_of_an_expression_is_proven_by_itself() {
    for ty in INTS.iter().filter(|t| t.bits == 64) {
        for (first, second, a) in [("+", "-", ty.max()), ("-", "+", ty.min())] {
            let source = with_header(format!(
                "fn main() ! console, alloc {{\n  a: {0} = {1}\n  b: {0} = 1\n  c: {0} = 1\n  print(\"{{a {first} b {second} c}}\")\n}}\n",
                ty.name, a
            ));
            let errors = check(source.as_bytes()).expect_err("the first operation does not fit");
            let goal = format!("a {first} b");
            assert!(
                errors.iter().any(|e| e.message.contains(&goal)),
                "{errors:?} does not state the goal about `{goal}`"
            );
            // The same with the first operation done on its own fits, and so
            // does the whole, so only the order of the proofs refuses it.
            let source = source.replace(&format!("= {a}"), "= 5");
            assert!(check(source.as_bytes()).is_ok(), "{source}");
        }
    }
}

/// A product of two values that are not constants is one unknown quantity,
/// so `x * y` is refused even where both are no more than 1, where its exact
/// range is not one that a type holds, as a product of two numbers of 64 bits.
fn a_product_of_two_values_that_are_not_constants_is_refused() {
    for ty in INTS.iter().filter(|t| t.bits == 64) {
        let source = with_header(format!(
            "fn f(x: {0}, y: {0}) -> {0} {{\n  if x >= 0 and x <= 1 and y >= 0 and y <= 1 {{\n    x * y\n  }} else {{\n    0\n  }}\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{f(1, 1)}}\")\n}}\n",
            ty.name
        ));
        let errors = check(source.as_bytes()).expect_err("a product of two unknowns");
        assert!(
            errors.iter().any(|e| e.message.contains("x * y")),
            "{errors:?} does not state the goal about `x * y`"
        );
        // With a number on one side, it is a sum and is proven.
        let source = source.replace("x * y", "x * 2");
        assert!(check(source.as_bytes()).is_ok(), "{source}");
    }
}

/// How many functions, blocks, and instructions a program holds.
fn size(program: &scalar::Program) -> (usize, usize, usize) {
    let blocks = program.functions.iter().map(|f| f.blocks.len()).sum();
    let instructions = (program.functions.iter())
        .flat_map(|f| &f.blocks)
        .map(|b| b.instructions.len())
        .sum();
    (program.functions.len(), blocks, instructions)
}

/// For every integer type of 64 bits and every operation that is accepted, the
/// scalar form is that of the program that gives its left operand instead, and
/// the operation, and the number it multiplies by, so nothing is added to test
/// the result.
fn an_accepted_operation_is_the_one_instruction_and_tests_nothing() {
    for ty in INTS.iter().filter(|t| t.bits == 64) {
        for op in OPS {
            let lowered = |operation: &str| {
                let source = with_header(format!(
                    "fn f(x: {0}, y: {0}) -> {0} {{\n  if x >= 1 and x <= 2 and y >= 0 and y <= 1 {{\n    {operation}\n  }} else {{\n    0\n  }}\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{f(2, 1)}}\")\n}}\n",
                    ty.name
                ));
                lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"))
            };
            let operand = if op == "*" { "2" } else { "y" };
            let (without, with) = (
                size(&lowered("x")),
                size(&lowered(&format!("x {op} {operand}"))),
            );
            assert_eq!(
                (with.0, with.1, with.2),
                (without.0, without.1, without.2 + 1 + usize::from(op == "*")),
                "for `{op}` on {}",
                ty.name
            );
        }
    }
}

/// An expression of numbers, written with the parentheses its precedence
/// needs and some it does not, and its value.
#[derive(Clone, Debug)]
enum Tree {
    Number(i128),
    Op(&'static str, Box<Tree>, Box<Tree>),
}

fn tree(g: &mut Gen, depth: usize) -> Tree {
    if depth == 0 || g.one_in(3) {
        return Tree::Number(g.below(10) as i128);
    }
    let op = *g.pick(&OPS);
    Tree::Op(
        op,
        Box::new(tree(g, depth - 1)),
        Box::new(tree(g, depth - 1)),
    )
}

fn binds(op: &str) -> u8 {
    u8::from(op == "*")
}

fn written(g: &mut Gen, t: &Tree) -> String {
    match t {
        Tree::Number(n) => n.to_string(),
        Tree::Op(op, l, r) => {
            let side = |g: &mut Gen, t: &Tree, right: bool| {
                let text = written(g, t);
                let needs = match t {
                    Tree::Op(inner, ..) => {
                        binds(inner) < binds(op) || (right && binds(inner) == binds(op))
                    }
                    Tree::Number(_) => false,
                };
                if needs || g.one_in(4) && matches!(t, Tree::Op(..)) {
                    format!("({text})")
                } else {
                    text
                }
            };
            let left = side(g, l, false);
            let right = side(g, r, true);
            format!("{left} {op} {right}")
        }
    }
}

fn value_of(t: &Tree) -> i128 {
    match t {
        Tree::Number(n) => *n,
        Tree::Op(op, l, r) => exact(op, value_of(l), value_of(r)).expect("small"),
    }
}

/// `*` groups before `+` and `-`, which group to the left, and parentheses
/// group as they say, so an expression of numbers has the value the
/// arithmetic of Rust gives for the same grouping.
fn operators_group_as_the_precedence_table_says() {
    generate::check(
        93,
        200,
        |g| {
            let t = tree(g, 3);
            let text = written(g, &t);
            (text, value_of(&t))
        },
        |(text, value)| {
            let source = with_header(format!("fn main() ! console, alloc {{\n  print(\"{{{text}}}\")\n}}\n"));
            assert_eq!(
                run(&source),
                Outcome {
                    stdout: format!("{value}\n").into_bytes(),
                    status: 0
                }
            );
        },
    );
}
