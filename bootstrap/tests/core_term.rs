//! The core term holds, in each node, its type and its place, and its parts
//! are the terms directly inside it. The terms here are written by hand, as
//! `sum_to` of `statements.md` is once it is checked.

use smith::core::{Callee, Kind, Lit, Make, Pattern, Term, Type};
use smith::diag::Pos;

use crate::Test;

pub const ONCE: &[Test] = &[
    (
        "a_term_is_written_out_with_each_of_its_parts_in_order",
        a_term_is_written_out_with_each_of_its_parts_in_order,
    ),
    (
        "the_size_of_a_term_is_the_nodes_of_every_part_and_itself",
        the_size_of_a_term_is_the_nodes_of_every_part_and_itself,
    ),
    (
        "every_node_of_a_term_holds_the_type_and_the_place_it_was_given",
        every_node_of_a_term_holds_the_type_and_the_place_it_was_given,
    ),
];

fn at(line: usize) -> Pos {
    Pos { line, col: 1 }
}

fn nat() -> Type {
    Type::int(0, u64::MAX as i128)
}

fn lit(n: i128, line: usize) -> Term {
    Term::new(Kind::Lit(Lit::Int(n)), nat(), at(line))
}

fn name(n: &str, line: usize) -> Term {
    Term::new(Kind::Name(n.to_string()), nat(), at(line))
}

fn call(f: usize, args: Vec<Term>, line: usize) -> Term {
    Term::new(Kind::Call { callee: Callee::Function(f), args }, nat(), at(line))
}

/// `if n == 0 { 0 } else { n + sum_to(n - 1) }`, where `==`, `+`, and `-` are
/// functions 0, 1, and 2, `sum_to` is function 3, and `false` and `true` are
/// the cases 0 and 1 of enum 0.
fn sum_to() -> Term {
    let step = call(
        1,
        vec![name("n", 3), call(3, vec![call(2, vec![name("n", 3), lit(1, 3)], 3)], 3)],
        3,
    );
    Term::new(
        Kind::Choose {
            of: Box::new(call(0, vec![name("n", 2), lit(0, 2)], 2)),
            arms: vec![
                (Pattern::Case(1, None), lit(0, 2)),
                (Pattern::Case(0, None), step),
            ],
        },
        nat(),
        at(2),
    )
}

fn a_term_is_written_out_with_each_of_its_parts_in_order() {
    assert_eq!(
        sum_to().show(),
        "(choose (call f0 n 0) .c1 => 0 .c0 => (call f1 n (call f3 (call f2 n 1))))"
    );
    let pair = Term::new(
        Kind::Make(Make::Tuple, vec![lit(1, 1), name("x", 1)]),
        Type::Tuple(vec![nat(), nat()]),
        at(1),
    );
    let bound = Term::new(
        Kind::Bind {
            name: "x".to_string(),
            value: Box::new(lit(7, 1)),
            rest: Box::new(Term::new(
                Kind::Field { of: Box::new(pair), name: "0".to_string(), index: 0 },
                nat(),
                at(2),
            )),
        },
        nat(),
        at(1),
    );
    assert_eq!(bound.show(), "(bind x 7 (field 0 (make tuple 1 x)))");
}

fn the_size_of_a_term_is_the_nodes_of_every_part_and_itself() {
    assert_eq!(lit(1, 1).size(), 1);
    // The `choose`, its test of three nodes, a block of one and a block of six.
    assert_eq!(sum_to().size(), 1 + 3 + 1 + 6);
    let lambda = Term::new(
        Kind::Lambda { params: vec![("n".to_string(), nat())], body: Box::new(sum_to()) },
        Type::Fn(vec![nat()], Box::new(nat())),
        at(1),
    );
    let apply = Term::new(
        Kind::Apply { function: Box::new(lambda), args: vec![lit(4, 1)] },
        nat(),
        at(1),
    );
    assert_eq!(apply.size(), 1 + (1 + sum_to().size()) + 1);
}

fn every_node_of_a_term_holds_the_type_and_the_place_it_was_given() {
    fn walk(t: &Term, seen: &mut Vec<(Pos, Type)>) {
        seen.push((t.pos, t.ty.clone()));
        for part in t.parts() {
            walk(part, seen);
        }
    }
    let mut seen = Vec::new();
    walk(&sum_to(), &mut seen);
    assert_eq!(seen.len(), sum_to().size());
    assert!(seen.iter().all(|(p, t)| p.line >= 2 && *t == nat()));
    assert_eq!(seen[0].0, at(2));
}
