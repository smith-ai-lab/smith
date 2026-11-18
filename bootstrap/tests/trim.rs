//! A function is passed only the values it reads, and gives back only the
//! values it makes. The pass that does this, `scalar::trim`, must change what a
//! function takes and gives and nothing a program prints.
//!
//! Each property builds a program of structs that are handed through calls,
//! and compares what it prints with and without the pass. The programs that
//! the first property builds also say how many parameters and results each
//! function must have after the passes, and the test counts them. The
//! generators work out both from plain sets of field numbers, which share
//! nothing with the compiler.

use smith::scalar::{
    self, Bits, Builder, Compare, Constant, Function, Op, Outcome, Program, Type, Binary,
};
use smith::lower;

use crate::Test;
use crate::generate::{self, Gen};
use crate::numbers::{INTS, Int, value};
use crate::targets::{runnable, runs_as};

pub const ONCE: &[Test] = &[
    (
        "a_function_of_a_chain_is_passed_what_it_reads_and_gives_back_what_it_changes",
        a_function_of_a_chain_is_passed_what_it_reads_and_gives_back_what_it_changes,
    ),
    (
        "a_chain_of_functions_that_hand_a_struct_on_prints_what_it_printed_without_the_pass",
        a_chain_of_functions_that_hand_a_struct_on_prints_what_it_printed_without_the_pass,
    ),
    (
        "a_parameter_that_is_read_on_one_path_only_is_still_passed",
        a_parameter_that_is_read_on_one_path_only_is_still_passed,
    ),
    (
        "a_result_that_differs_on_one_return_only_is_still_given_back",
        a_result_that_differs_on_one_return_only_is_still_given_back,
    ),
    (
        "a_struct_held_in_a_var_across_a_loop_is_read_whole_after_the_calls",
        a_struct_held_in_a_var_across_a_loop_is_read_whole_after_the_calls,
    ),
    (
        "a_function_called_from_two_places_gives_each_the_values_it_was_given",
        a_function_called_from_two_places_gives_each_the_values_it_was_given,
    ),
    (
        "a_function_that_allocates_on_one_path_only_leaves_the_memory_of_its_caller_alone",
        a_function_that_allocates_on_one_path_only_leaves_the_memory_of_its_caller_alone,
    ),
    (
        "a_value_given_back_in_another_place_than_it_came_in_is_the_value_the_caller_gave",
        a_value_given_back_in_another_place_than_it_came_in_is_the_value_the_caller_gave,
    ),
    (
        "a_function_that_gives_back_one_parameter_twice_gives_the_caller_it_twice",
        a_function_that_gives_back_one_parameter_twice_gives_the_caller_it_twice,
    ),
    (
        "a_function_whose_first_block_is_a_loop_gives_back_what_the_loop_made",
        a_function_whose_first_block_is_a_loop_gives_back_what_the_loop_made,
    ),
    (
        "a_function_in_a_ring_of_calls_is_left_as_it_is",
        a_function_in_a_ring_of_calls_is_left_as_it_is,
    ),
    (
        "what_only_a_block_that_nothing_reaches_gives_back_or_reads_is_not_counted",
        what_only_a_block_that_nothing_reaches_gives_back_or_reads_is_not_counted,
    ),
    (
        "a_main_that_reads_nothing_it_is_given_is_still_given_it",
        a_main_that_reads_nothing_it_is_given_is_still_given_it,
    ),
    (
        "a_parameter_that_a_function_only_hands_on_in_a_loop_to_itself_is_not_passed",
        a_parameter_that_a_function_only_hands_on_in_a_loop_to_itself_is_not_passed,
    ),
];

pub const MACHINE: &[Test] = &[
    (
        "every_executable_of_a_chain_of_functions_that_hand_a_struct_on_prints_what_the_evaluator_says",
        every_executable_of_a_chain_of_functions_that_hand_a_struct_on_prints_what_the_evaluator_says,
    ),
    (
        "every_executable_of_the_programs_that_trim_must_not_break_prints_what_the_evaluator_says",
        every_executable_of_the_programs_that_trim_must_not_break_prints_what_the_evaluator_says,
    ),
];

/// A chain of functions of a struct of `types.len()` fields, `f0` and so on.
/// The first field is an `i64`, and each function changes it with a number
/// that is its own, which is how the test finds the function in the program
/// after the passes. Function `l` changes the fields in `changed[l]`, reads
/// the fields in `read[l]` to choose what it adds to the first, and hands the
/// struct to function `l - 1`, if there is one. The first function is the one
/// that calls no other.
#[derive(Debug)]
struct Chain {
    types: Vec<Int>,
    changed: Vec<Vec<usize>>,
    read: Vec<Vec<usize>>,
    /// The values of the fields for each call that `main` makes.
    starts: Vec<Vec<i128>>,
}

/// The number function `l` multiplies the first field by.
fn mark(l: usize) -> u64 {
    1_000_003 + 17 * l as u64
}

fn chain(g: &mut Gen) -> Chain {
    let count = 1 + g.below(24);
    let mut types = vec![INTS[6]];
    for _ in 1..count {
        types.push(*g.pick(INTS));
    }
    let depth = 1 + g.below(4);
    let mut changed = Vec::new();
    let mut read = Vec::new();
    for _ in 0..depth {
        let mut c = vec![0];
        let mut r = Vec::new();
        for field in 1..count {
            if g.one_in(2) {
                c.push(field);
            } else if g.one_in(3) {
                r.push(field);
            }
        }
        changed.push(c);
        read.push(r);
    }
    // One call of each function, and a second of the last, so that no
    // function is called from one place only.
    let starts = (0..depth + 1)
        .map(|_| types.iter().map(|t| value(g, *t)).collect())
        .collect();
    Chain {
        types,
        changed,
        read,
        starts,
    }
}

impl Chain {
    /// The fields function `l` must be passed, by the model: those it
    /// changes and reads, and those the function it calls is passed.
    fn params(&self, l: usize) -> Vec<usize> {
        let mut fields: Vec<usize> = (self.changed[l].iter().chain(&self.read[l]))
            .copied()
            .collect();
        if l > 0 {
            fields.extend(self.params(l - 1));
        }
        fields.sort();
        fields.dedup();
        fields
    }

    /// The fields function `l` must give back: those it changes and those the
    /// function it calls changes.
    fn results(&self, l: usize) -> Vec<usize> {
        let mut fields = self.changed[l].clone();
        if l > 0 {
            fields.extend(self.results(l - 1));
        }
        fields.sort();
        fields.dedup();
        fields
    }

    fn source(&self) -> String {
        let mut s = String::from("use std.int (wrapping_add, wrapping_mul)\n\nstruct S {\n");
        for (i, t) in self.types.iter().enumerate() {
            s.push_str(&format!("  f{i}: {}\n", t.name));
        }
        s.push_str("}\n");
        for l in 0..self.changed.len() {
            s.push_str(&format!("\nfn g{l}(s: S) -> S {{\n  var t = s\n"));
            s.push_str(&format!(
                "  var e = wrapping_add(wrapping_mul(s.f0, {}), 1)\n",
                mark(l)
            ));
            for q in &self.read[l] {
                s.push_str(&format!("  if s.f{q} > 2 {{ e = wrapping_add(e, 1) }}\n"));
            }
            s.push_str("  t.f0 = e\n");
            for c in self.changed[l].iter().skip(1) {
                s.push_str(&format!("  t.f{c} = wrapping_add(s.f{c}, 3)\n"));
            }
            if l == 0 {
                s.push_str("  t\n}\n");
            } else {
                s.push_str(&format!("  g{}(t)\n}}\n", l - 1));
            }
        }
        s.push_str("\nfn main() ! console, alloc {\n");
        let depth = self.changed.len();
        for (n, start) in self.starts.iter().enumerate() {
            let function = if n < depth { n } else { depth - 1 };
            let fields: Vec<String> = (start.iter().enumerate())
                .map(|(i, v)| format!("f{i}: {v}"))
                .collect();
            s.push_str(&format!(
                "  var a{n} = S({})\n  a{n} = g{function}(a{n})\n",
                fields.join(", ")
            ));
            for i in 0..self.types.len() {
                s.push_str(&format!("  print(\"{{a{n}.f{i}}}\")\n"));
            }
        }
        s.push_str("}\n");
        s
    }
}

fn built(source: &str) -> Program {
    lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"))
}

/// The function of the program that holds a multiplication by `n`, which is
/// how a test finds a function it wrote after the passes have had their way.
fn holding(program: &Program, n: u64) -> Option<&Function> {
    program.functions.iter().find(|f| {
        (f.blocks.iter().flat_map(|b| &b.instructions))
            .any(|i| i.op == Op::Constant(Constant::Int(n)))
    })
}

fn a_function_of_a_chain_is_passed_what_it_reads_and_gives_back_what_it_changes() {
    generate::check(41, 14, chain, |c: &Chain| {
        let source = c.source();
        let after = scalar::passes(&built(&source));
        for l in 0..c.changed.len() {
            let f = holding(&after, mark(l))
                .unwrap_or_else(|| panic!("function {l} is gone from {after:?} for {source}"));
            assert_eq!(
                f.blocks[0].params.len(),
                c.params(l).len(),
                "parameters of function {l} for {c:?}"
            );
            assert_eq!(
                f.results.len(),
                c.results(l).len(),
                "results of function {l} for {c:?}"
            );
        }
    });
}

/// What `program` prints with each pass alone and with all of them, which must
/// be what it prints with none.
fn prints_the_same_with_the_pass(program: &Program, said: &Outcome) {
    let inlined = scalar::inline(program);
    assert_eq!(&scalar::run(&scalar::trim(&inlined)), said);
    assert_eq!(&scalar::run(&scalar::passes(program)), said);
}

fn a_chain_of_functions_that_hand_a_struct_on_prints_what_it_printed_without_the_pass() {
    generate::check(42, 14, chain, |c: &Chain| {
        let source = c.source();
        let program = built(&source);
        let said = scalar::run(&program);
        assert_eq!(said.status, 0, "for {source}");
        let lines = said.stdout.iter().filter(|b| **b == b'\n').count();
        assert_eq!(lines, c.starts.len() * c.types.len(), "for {source}");
        prints_the_same_with_the_pass(&program, &said);
    });
}

fn every_executable_of_a_chain_of_functions_that_hand_a_struct_on_prints_what_the_evaluator_says() {
    let targets = runnable();
    generate::check(43, 4, chain, |c: &Chain| {
        let source = c.source();
        let said = scalar::run(&built(&source));
        for &target in &targets {
            runs_as(source.as_bytes(), target, &said);
        }
    });
}

/// A program a person wrote, what it must print, and what its function must
/// be passed and give back after the passes.
struct Written {
    source: &'static str,
    prints: &'static [&'static str],
    /// A number the function multiplies by, to find it again.
    mark: u64,
    params: usize,
    results: usize,
}

const READ_ON_ONE_PATH: Written = Written {
    source: "use std.int (wrapping_add, wrapping_mul)

fn pick(a: i64, b: i64, c: i64) -> i64 {
  var r = wrapping_mul(wrapping_add(c, 1), 424242)
  if a > 0 { r = wrapping_add(r, b) }
  r
}

fn main() ! console, alloc {
  print(\"{pick(1, 10, 100)}\")
  print(\"{pick(0, 10, 100)}\")
  print(\"{pick(1, 20, 7)}\")
}
",
    prints: &["42848452", "42848442", "3393956"],
    mark: 424242,
    params: 3,
    results: 1,
};

const DIFFERS_ON_ONE_RETURN: Written = Written {
    source: "use std.int (wrapping_add, wrapping_mul)

struct P {
  x: i64
  y: i64
}

fn step(p: P) -> P {
  if p.x > 5 { p } else { P(x: wrapping_mul(wrapping_add(p.x, 1), 515151), y: p.y) }
}

fn main() ! console, alloc {
  a = step(P(x: 1, y: 9))
  b = step(P(x: 7, y: 4))
  c = step(step(P(x: 0, y: 3)))
  print(\"{a.x} {a.y}\")
  print(\"{b.x} {b.y}\")
  print(\"{c.x} {c.y}\")
}
",
    prints: &["1030302 9", "7 4", "515151 3"],
    mark: 515151,
    params: 1,
    results: 1,
};

const LOOP_OVER_A_VAR: Written = Written {
    source: "use std.int (wrapping_add, wrapping_mul)

struct S {
  a: i64
  b: i64
  c: i64
}

fn step(s: S) -> S {
  var t = s
  t.a = wrapping_add(s.a, s.b)
  t.b = wrapping_mul(wrapping_add(s.b, 1), 616161)
  t
}

fn main() ! console, alloc {
  var s = S(a: 2, b: 3, c: 5)
  loop i in 0..4 { s = step(s) }
  print(\"{s.a} {s.b} {s.c}\")
  u = step(S(a: 7, b: 1, c: 9))
  print(\"{u.a} {u.b} {u.c}\")
}
",
    prints: &[
        "935714782892311700 -2965396865257453753 5",
        "8 1232322 9",
    ],
    mark: 616161,
    params: 2,
    results: 2,
};

const TWO_PLACES: Written = Written {
    source: "use std.int (wrapping_add, wrapping_mul)

struct S {
  a: i64
  b: i64
  c: i64
}

fn bump(s: S) -> S {
  var t = s
  t.a = wrapping_mul(wrapping_add(wrapping_add(s.a, s.b), s.c), 717171)
  t
}

fn main() ! console, alloc {
  var x = S(a: 1, b: 2, c: 3)
  var y = S(a: 10, b: 20, c: 30)
  x = bump(x)
  y = bump(y)
  print(\"{x.a} {x.b} {x.c}\")
  print(\"{y.a} {y.b} {y.c}\")
}
",
    prints: &["4303026 2 3", "43030260 20 30"],
    mark: 717171,
    params: 3,
    results: 1,
};

/// A function that allocates on one path only, and gives back what it made.
/// What `main` holds before and after the calls is read at the end.
const ALLOCATES_ON_ONE_PATH: Written = Written {
    source: "use std.int (wrapping_add)

fn make(n: u64) -> List[u64] ! alloc {
  var xs: List[u64] = []
  if n > 3 {
    xs = push(xs, wrapping_add(n, 8888))
    xs = push(xs, wrapping_add(n, 9999))
  }
  xs
}

fn main() ! console, alloc {
  var first: List[u64] = []
  first = push(first, 11)
  a = make(5)
  b = make(1)
  c = make(9)
  var last: List[u64] = []
  last = push(last, 22)
  loop v in first { print(\"{v}\") }
  loop v in a { print(\"{v}\") }
  print(\"{b.size}\")
  loop v in c { print(\"{v}\") }
  loop v in last { print(\"{v}\") }
}
",
    prints: &["11", "8893", "10004", "0", "8897", "10008", "22"],
    mark: 8888,
    params: 0,
    results: 0,
};

const SWAPPED: Written = Written {
    source: "use std.int (wrapping_add, wrapping_mul)

struct T {
  x: i64
  y: i64
  z: i64
}

fn swap(t: T) -> T {
  T(x: t.y, y: t.x, z: wrapping_mul(wrapping_add(t.z, 3), 818181))
}

fn main() ! console, alloc {
  p = swap(T(x: 1, y: 2, z: 3))
  q = swap(T(x: 10, y: 20, z: 30))
  print(\"{p.x} {p.y} {p.z}\")
  print(\"{q.x} {q.y} {q.z}\")
}
",
    prints: &["2 1 4909086", "20 10 26999973"],
    mark: 818181,
    params: 1,
    results: 1,
};

const TWICE: Written = Written {
    source: "use std.int (wrapping_add, wrapping_mul)

struct T {
  x: i64
  y: i64
  z: i64
}

fn twice(t: T) -> T {
  T(x: t.x, y: t.x, z: wrapping_mul(wrapping_add(t.z, 3), 929292))
}

fn main() ! console, alloc {
  p = twice(T(x: 5, y: 6, z: 7))
  q = twice(T(x: -8, y: 1, z: 0))
  print(\"{p.x} {p.y} {p.z}\")
  print(\"{q.x} {q.y} {q.z}\")
}
",
    prints: &["5 5 9292920", "-8 -8 2787876"],
    mark: 929292,
    params: 1,
    results: 1,
};

/// Every program a person wrote above.
const WRITTEN: &[&Written] = &[
    &READ_ON_ONE_PATH,
    &DIFFERS_ON_ONE_RETURN,
    &LOOP_OVER_A_VAR,
    &TWO_PLACES,
    &ALLOCATES_ON_ONE_PATH,
    &SWAPPED,
    &TWICE,
];

/// The program prints what it must with and without the pass, and, where the
/// function it holds is still a function after the passes, takes and gives
/// what the pass leaves it.
fn written_prints_what_it_must(w: &Written) {
    let program = built(w.source);
    let said = scalar::run(&program);
    let printed = String::from_utf8(said.stdout.clone()).expect("text");
    let printed: Vec<&str> = printed.lines().collect();
    assert_eq!(printed, w.prints, "without the pass, for {}", w.source);
    prints_the_same_with_the_pass(&program, &said);
    let after = scalar::passes(&program);
    let f = holding(&after, w.mark)
        .unwrap_or_else(|| panic!("the function is inlined, so the test tests nothing: {after:?}"));
    if w.params + w.results > 0 {
        assert_eq!(f.blocks[0].params.len(), w.params, "parameters in {after:?}");
        assert_eq!(f.results.len(), w.results, "results in {after:?}");
    }
}

fn a_parameter_that_is_read_on_one_path_only_is_still_passed() {
    written_prints_what_it_must(&READ_ON_ONE_PATH);
}

fn a_result_that_differs_on_one_return_only_is_still_given_back() {
    written_prints_what_it_must(&DIFFERS_ON_ONE_RETURN);
}

fn a_struct_held_in_a_var_across_a_loop_is_read_whole_after_the_calls() {
    written_prints_what_it_must(&LOOP_OVER_A_VAR);
}

fn a_function_called_from_two_places_gives_each_the_values_it_was_given() {
    written_prints_what_it_must(&TWO_PLACES);
}

fn a_function_that_allocates_on_one_path_only_leaves_the_memory_of_its_caller_alone() {
    written_prints_what_it_must(&ALLOCATES_ON_ONE_PATH);
}

fn a_value_given_back_in_another_place_than_it_came_in_is_the_value_the_caller_gave() {
    written_prints_what_it_must(&SWAPPED);
}

fn a_function_that_gives_back_one_parameter_twice_gives_the_caller_it_twice() {
    written_prints_what_it_must(&TWICE);
}

fn every_executable_of_the_programs_that_trim_must_not_break_prints_what_the_evaluator_says() {
    let targets = runnable();
    for w in WRITTEN {
        let said = scalar::run(&built(w.source));
        for &target in &targets {
            runs_as(w.source.as_bytes(), target, &said);
        }
    }
}

const INT: Type = Type::Int(Bits::B64);

/// A program of the functions, each given, and a `main` that does nothing.
fn program_of(mut functions: Vec<Function>) -> Program {
    let mut main = Builder::new();
    main.ret(Vec::new());
    functions.push(main.finish(&[]));
    Program {
        data: Vec::new(),
        main: functions.len() - 1,
        functions,
    }
}

/// `f(a, n)` adds 1 to `a` and takes 1 from `n` until `n` is 0, by jumping to
/// its own first block, and gives back `a`. `g(x)` gives back `f(x, 3)`.
fn a_function_whose_first_block_is_a_loop_gives_back_what_the_loop_made() {
    let mut f = Builder::new();
    let p = f.params(&[INT, INT]);
    let zero = f.int(Bits::B64, 0);
    let done = f.compare(Compare::Equal, p[1], zero);
    let (exit, _) = f.block(&[]);
    let (again, _) = f.block(&[]);
    f.switch(done, vec![(again, vec![]), (exit, vec![])]);
    f.at(again);
    let one = f.int(Bits::B64, 1);
    let a = f.binary(Binary::Add, p[0], one);
    let n = f.binary(Binary::Sub, p[1], one);
    f.jump(0, vec![a, n]);
    f.at(exit);
    f.ret(vec![p[0]]);
    let f = f.finish(&[INT]);

    let mut g = Builder::new();
    let x = g.params(&[INT]);
    let three = g.int(Bits::B64, 3);
    let made = g.call(0, vec![x[0], three], &[INT]);
    g.ret(made);
    let g = g.finish(&[INT]);

    let program = program_of(vec![f, g]);
    assert_eq!(scalar::check(&program), Ok(()));
    let trimmed = scalar::trim(&program);
    assert_eq!(scalar::check(&trimmed), Ok(()));
    for start in [0u64, 5, 1 << 40] {
        let expected = scalar::call(&program, 1, &[start]);
        assert_eq!(expected, vec![start + 3]);
        assert_eq!(scalar::call(&trimmed, 1, &[start]), expected);
    }
}

/// Two functions that call each other, of which the first is passed a value
/// it never reads. The form has no such functions yet, and the pass leaves
/// both as they are.
fn a_function_in_a_ring_of_calls_is_left_as_it_is() {
    let mut f = Builder::new();
    let p = f.params(&[INT, INT]);
    let made = f.call(1, vec![p[0]], &[INT]);
    f.ret(made);
    let f = f.finish(&[INT]);
    let mut g = Builder::new();
    let x = g.params(&[INT]);
    let made = g.call(0, vec![x[0], x[0]], &[INT]);
    g.ret(made);
    let g = g.finish(&[INT]);
    let program = program_of(vec![f, g]);
    assert_eq!(scalar::trim(&program), program);
}

/// `f(a, b)` gives back `a`. A block that nothing reaches gives back `b`
/// after a call that reads it. So `f` is passed nothing and gives nothing
/// back, and `g(x, y)`, which gives back `f(x, y)` and 1 more, is passed `x`.
fn what_only_a_block_that_nothing_reaches_gives_back_or_reads_is_not_counted() {
    let mut f = Builder::new();
    let p = f.params(&[INT, INT]);
    let (unreached, _) = f.block(&[]);
    f.ret(vec![p[0]]);
    f.at(unreached);
    f.call(1, vec![p[1]], &[INT]);
    f.ret(vec![p[1]]);
    let f = f.finish(&[INT]);

    let mut h = Builder::new();
    let x = h.params(&[INT]);
    let one = h.int(Bits::B64, 1);
    let next = h.binary(Binary::Add, x[0], one);
    h.ret(vec![next]);
    let h = h.finish(&[INT]);

    let mut g = Builder::new();
    let p = g.params(&[INT, INT]);
    let made = g.call(0, p, &[INT]);
    let one = g.int(Bits::B64, 1);
    let more = g.binary(Binary::Add, made[0], one);
    g.ret(vec![more]);
    let g = g.finish(&[INT]);

    let program = program_of(vec![f, h, g]);
    assert_eq!(scalar::check(&program), Ok(()));
    let trimmed = scalar::trim(&program);
    assert_eq!(scalar::check(&trimmed), Ok(()));
    assert_eq!(trimmed.functions[0].blocks[0].params.len(), 0, "{trimmed:?}");
    assert_eq!(trimmed.functions[0].results.len(), 0, "{trimmed:?}");
    assert_eq!(trimmed.functions[2].blocks[0].params.len(), 1, "{trimmed:?}");
    assert_eq!(scalar::call(&trimmed, 2, &[4]), vec![5]);
}

fn a_main_that_reads_nothing_it_is_given_is_still_given_it() {
    let mut main = Builder::new();
    main.params(&scalar::ARGUMENTS);
    main.ret(Vec::new());
    let program = Program {
        data: Vec::new(),
        main: 0,
        functions: vec![main.finish(&[])],
    };
    assert!(program.takes_arguments());
    assert!(scalar::trim(&program).takes_arguments());
    assert!(scalar::passes(&program).takes_arguments());

    let source = "fn main(args: List[str]) ! console, alloc { print(\"done\") }\n";
    let program = built(source);
    assert!(program.takes_arguments(), "{program:?}");
    let after = scalar::passes(&program);
    assert!(after.takes_arguments(), "{after:?}");
    let words = vec![b"one".to_vec(), b"two".to_vec()];
    assert_eq!(
        scalar::run_with_words(&after, &words),
        scalar::run_with_words(&program, &words)
    );
}

/// `f(a, b)` jumps to a loop that hands `b` to itself while it counts `a`
/// down, and gives back `a`. Nothing reads `b`, though a jump gives it.
fn a_parameter_that_a_function_only_hands_on_in_a_loop_to_itself_is_not_passed() {
    let mut f = Builder::new();
    let p = f.params(&[INT, INT]);
    let (head, h) = f.block(&[INT, INT]);
    f.jump(head, vec![p[0], p[1]]);
    f.at(head);
    let zero = f.int(Bits::B64, 0);
    let done = f.compare(Compare::Equal, h[0], zero);
    let (exit, _) = f.block(&[]);
    let (again, _) = f.block(&[]);
    f.switch(done, vec![(again, vec![]), (exit, vec![])]);
    f.at(again);
    let one = f.int(Bits::B64, 1);
    let a = f.binary(Binary::Sub, h[0], one);
    f.jump(head, vec![a, h[1]]);
    f.at(exit);
    f.ret(vec![h[0]]);
    let f = f.finish(&[INT]);

    let mut g = Builder::new();
    let x = g.params(&[INT]);
    let seven = g.int(Bits::B64, 7);
    let made = g.call(0, vec![x[0], seven], &[INT]);
    g.ret(made);
    let g = g.finish(&[INT]);

    let program = program_of(vec![f, g]);
    assert_eq!(scalar::check(&program), Ok(()));
    let trimmed = scalar::trim(&program);
    assert_eq!(scalar::check(&trimmed), Ok(()));
    assert_eq!(trimmed.functions[0].blocks[0].params.len(), 1, "{trimmed:?}");
    assert_eq!(scalar::call(&trimmed, 1, &[4]), vec![0]);
}
