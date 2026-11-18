//! `try e` splits `e` into going on or leaving, and returns from the
//! function on the second, so it is written anywhere an expression is.

use smith::diag::Error;
use smith::lower;
use smith::scalar::{self, Outcome};

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "try_in_a_loop_reads_the_elements_up_to_the_first_failure_and_no_more",
        try_in_a_loop_reads_the_elements_up_to_the_first_failure_and_no_more,
    ),
    (
        "try_written_anywhere_prints_what_the_function_prints_with_the_match_it_means",
        try_written_anywhere_prints_what_the_function_prints_with_the_match_it_means,
    ),
    (
        "a_value_the_function_owns_is_used_after_a_try_as_before_it",
        a_value_the_function_owns_is_used_after_a_try_as_before_it,
    ),
    (
        "a_case_that_holds_nothing_is_the_case_that_holds_unit_in_building_and_in_a_pattern",
        a_case_that_holds_nothing_is_the_case_that_holds_unit_in_building_and_in_a_pattern,
    ),
    (
        "try_has_no_then_to_write_and_a_try_in_a_lambda_returns_from_the_lambda",
        try_has_no_then_to_write_and_a_try_in_a_lambda_returns_from_the_lambda,
    ),
];

fn run(source: &str) -> Outcome {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} in {source}"));
    scalar::run(&program)
}

fn errors(source: &str) -> Vec<Error> {
    lower(source.as_bytes()).expect_err("an error")
}

/// What a program prints, one line for each of `lines`.
fn says(lines: &[String]) -> Outcome {
    let mut stdout = lines.join("\n").into_bytes();
    stdout.push(b'\n');
    Outcome { stdout, status: 0 }
}

/// `first_bad` reads the elements of a list in order with `try` in a loop,
/// and gives the first failure after reading that element and no later one.
fn try_in_a_loop_reads_the_elements_up_to_the_first_failure_and_no_more() {
    let lists = |g: &mut Gen| {
        let size = g.below(6);
        let bad = g.below(size + 2);
        (size, bad)
    };
    generate::check(801, 24, lists, |&(size, bad)| {
        let elements: Vec<String> = (0..size)
            .map(|i| if i == bad { "Result.err(\"x\")".to_string() } else { format!("Result.ok({})", i + 1) })
            .collect();
        let source = format!(
            "fn read(r: Result[u64, str]) -> Result[u64, str] ! console {{\n  print(\"read\")\n  r\n}}\n\n\
             fn first_bad(xs: List[Result[u64, str]]) -> Result[u64, str] ! console {{\n  var total: u64 = 0\n  \
             loop i in 0..xs.size {{\n    v = try read(xs[i])\n    total = wrapping_add(total, v)\n  }}\n  .ok(total)\n}}\n\n\
             fn main() ! console, alloc {{\n  xs: List[Result[u64, str]] = [{}]\n  match first_bad(xs) {{\n    \
             .ok(t) {{ print(\"ok {{t}}\") }}\n    .err(e) {{ print(\"err {{e}}\") }}\n  }}\n}}\n",
            elements.join(", ")
        );
        let mut lines: Vec<String> = Vec::new();
        let mut total = 0;
        let mut failed = false;
        for i in 0..size {
            lines.push("read".into());
            if i == bad {
                failed = true;
                break;
            }
            total += i + 1;
        }
        lines.push(if failed { "err x".into() } else { format!("ok {total}") });
        assert_eq!(run(&source), says(&lines), "{source}");
    });
}

/// A type that a `try` is written over, with what its functions are called.
struct Family {
    /// What the function that has the `try` gives.
    ty: &'static str,
    /// The definitions the program needs besides `step`.
    extra: &'static str,
    /// What `step` gives when `n < cut`, and when it does not.
    go: &'static str,
    stop: &'static str,
    /// The function that gives a success.
    ok: &'static str,
    /// The arms of `show`.
    arms: &'static str,
    /// What `show` prints for a failure of `n`.
    failed: fn(u64) -> String,
}

const FAMILIES: [Family; 4] = [
    Family {
        ty: "Option[u64]",
        extra: "",
        go: ".some(n)",
        stop: ".none",
        ok: ".some",
        arms: ".some(v) { print(\"ok {v}\") }\n    .none { print(\"fail\") }",
        failed: |_| "fail".to_string(),
    },
    Family {
        ty: "Result[u64, u64]",
        extra: "",
        go: ".ok(n)",
        stop: ".err(n)",
        ok: ".ok",
        arms: ".ok(v) { print(\"ok {v}\") }\n    .err(n) { print(\"fail {n}\") }",
        failed: |n| format!("fail {n}"),
    },
    Family {
        ty: "Flow[u64, u64]",
        extra: "",
        go: ".go(n)",
        stop: ".stop(n)",
        ok: ".go",
        arms: ".go(v) { print(\"ok {v}\") }\n    .stop(n) { print(\"fail {n}\") }",
        failed: |n| format!("fail {n}"),
    },
    Family {
        ty: "Mine",
        extra: "enum Mine {\n  good: u64\n  bad: u64\n}\n\n\
                impl Try[Mine, Mine, u64] {\n  fn split(c: take Mine) -> Flow[u64, Mine] {\n    match c {\n      \
                .good(x) { .go(x) }\n      .bad(n) { .stop(.bad(n)) }\n    }\n  }\n}\n\n",
        go: ".good(n)",
        stop: ".bad(n)",
        ok: ".good",
        arms: ".good(v) { print(\"ok {v}\") }\n    .bad(n) { print(\"fail {n}\") }",
        failed: |n| format!("fail {n}"),
    },
];

/// Where a `try` is written: the body of a function that has one or two,
/// as `@0` and `@1`, the last name it works out as `@V`, what each `try`
/// goes over, and what the function gives for a `cut`.
struct Place {
    body: &'static str,
    operands: &'static [&'static str],
    last: &'static str,
    /// `Ok(v)` when it succeeds, `Err(n)` when it leaves.
    gives: fn(u64) -> Result<u64, u64>,
}

const PLACES: [Place; 5] = [
    // In a loop.
    Place {
        body: "  var total: u64 = 0\n  loop i in 0..4 {\n    v = @0\n    total = wrapping_add(total, v)\n  }\n",
        operands: &["step(i, cut)"],
        last: "total",
        gives: |cut| if cut >= 4 { Ok(6) } else { Err(cut) },
    },
    // In a loop inside a loop.
    Place {
        body: "  lim: u64 = 3\n  var total: u64 = 0\n  loop i in 0..lim {\n    loop j in 0..lim {\n      v = @0\n      total = wrapping_add(total, v)\n    }\n  }\n",
        operands: &["step(wrapping_add(wrapping_mul(i, 3), j), cut)"],
        last: "total",
        gives: |cut| if cut >= 9 { Ok(36) } else { Err(cut) },
    },
    // In an arm of a `match`.
    Place {
        body: "  x = match cut {\n    0 { 100 }\n    n { @0 }\n  }\n",
        operands: &["step(5, n)"],
        last: "x",
        gives: |cut| match cut {
            0 => Ok(100),
            c if c > 5 => Ok(5),
            _ => Err(5),
        },
    },
    // In a block of an `if`.
    Place {
        body: "  x = if cut < 3 { @0 } else { 7 }\n",
        operands: &["step(cut, cut)"],
        last: "x",
        gives: |cut| if cut < 3 { Err(cut) } else { Ok(7) },
    },
    // In an argument of a call, twice.
    Place {
        body: "  r = wrapping_add(@0, @1)\n",
        operands: &["step(1, cut)", "step(2, cut)"],
        last: "r",
        gives: |cut| match cut {
            0 | 1 => Err(1),
            2 => Err(2),
            _ => Ok(3),
        },
    },
];

/// Every family with every place.
fn every_pair() -> impl Iterator<Item = (usize, usize)> {
    (0..FAMILIES.len()).flat_map(|f| (0..PLACES.len()).map(move |p| (f, p)))
}

/// `try operand`, or the `match` it means, whose names end in `at`.
fn written(operand: &str, at: usize, matched: bool) -> String {
    if matched {
        format!("match split({operand}) {{\n    .go(g{at}) {{ g{at} }}\n    .stop(s{at}) {{ return s{at} }}\n  }}")
    } else {
        format!("try {operand}")
    }
}

/// The function `f` for a family at a place, with each `try` written as `try`
/// or as the `match` on what `split` gives, which `Try.split` is wrapped in so
/// that what the function gives settles what it splits into.
fn program(family: &Family, place: &Place, matched: bool) -> String {
    let mut body = place.body.to_string();
    for (at, operand) in place.operands.iter().enumerate() {
        body = body.replace(&format!("@{at}"), &written(operand, at, matched));
    }
    let ty = family.ty;
    let mut cuts = String::new();
    for cut in 0..=10 {
        cuts.push_str(&format!("  show(f({cut}))\n"));
    }
    format!(
        "{extra}fn step(n: u64, cut: u64) -> {ty} {{\n  if n < cut {{ {go} }} else {{ {stop} }}\n}}\n\n\
         fn split(c: take {ty}) -> Flow[u64, {ty}] {{ Try.split(c) }}\n\n\
         fn f(cut: u64) -> {ty} {{\n{body}  {ok}({last})\n}}\n\n\
         fn show(r: {ty}) ! console, alloc {{\n  match r {{\n    {arms}\n  }}\n}}\n\n\
         fn main() ! console, alloc {{\n{cuts}}}\n",
        extra = family.extra,
        go = family.go,
        stop = family.stop,
        ok = family.ok,
        last = place.last,
        arms = family.arms,
    )
}

/// For a function that has `try` in a loop, in a loop inside a loop, in an
/// arm of a `match`, in a block of an `if`, and in an argument of a call,
/// over an `Option`, a `Result`, a `Flow`, and a type of its own, the program
/// prints what the same function prints with each `try` written as the
/// `match` on what `split` gives, which goes on with `.go` and returns the
/// `.stop`, and what the places say.
fn try_written_anywhere_prints_what_the_function_prints_with_the_match_it_means() {
    for (f, p) in every_pair() {
        let (family, place) = (&FAMILIES[f], &PLACES[p]);
        let expected: Vec<String> = (0..=10)
            .map(|cut| match (place.gives)(cut) {
                Ok(v) => format!("ok {v}"),
                Err(n) => (family.failed)(n),
            })
            .collect();
        let sugar = program(family, place, false);
        let matched = program(family, place, true);
        assert_eq!(run(&sugar), says(&expected), "{sugar}");
        assert_eq!(run(&matched), says(&expected), "{matched}");
    }
}

/// A value the function owns, without `Copy`, is given away after a `try`
/// that is written anywhere, and the ownership check sees no lambda. Used
/// again after it was given away, it is one error at that use.
fn a_value_the_function_owns_is_used_after_a_try_as_before_it() {
    for (f, p) in every_pair() {
        let (family, place) = (&FAMILIES[f], &PLACES[p]);
        let source = |again: bool| {
            let sugar = program(family, place, false);
            let give = format!(
                "  n = consume(k)\n{}  {ok}(wrapping_add({last}, n))\n}}",
                if again { "  m = consume(k)\n" } else { "" },
                ok = family.ok,
                last = place.last
            );
            let ending = format!("  {}({})\n}}", family.ok, place.last);
            let owned = sugar
                .replace("fn f(cut: u64)", "fn f(k: take Key, cut: u64)")
                .replace(&ending, &give)
                .replace("show(f(", "show(f(Key(secret: 4), ");
            format!(
                "struct Key {{\n  secret: u64\n}}\n\nwithout Copy for Key\n\n\
                 fn consume(k: take Key) -> u64 {{ k.secret }}\n\n{owned}"
            )
        };
        let expected: Vec<String> = (0..=10)
            .map(|cut| match (place.gives)(cut) {
                Ok(v) => format!("ok {}", v + 4),
                Err(n) => (family.failed)(n),
            })
            .collect();
        let accepted = source(false);
        assert_eq!(run(&accepted), says(&expected), "{accepted}");
        let refused = source(true);
        let found = errors(&refused);
        assert_eq!(found.len(), 1, "{found:?} in {refused}");
        assert!(found[0].message.contains("`k`"), "{}", found[0]);
        let line = refused.lines().position(|l| l == "  m = consume(k)").expect("a line") + 1;
        assert_eq!(found[0].pos.line, line, "{}", found[0]);
    }
}

/// A case that holds nothing and a case that holds `()` are one thing: it is
/// built and matched written `.more`, and `.more(())` is the same.
fn a_case_that_holds_nothing_is_the_case_that_holds_unit_in_building_and_in_a_pattern() {
    generate::check(804, 8, |g| g.below(2) == 0, |&more| {
        let program = |built: &str, matched: &str| {
            format!(
                "enum Step[T] {{\n  more: T\n  done\n}}\n\n\
                 fn make(more: bool) -> Step[()] {{\n  if more {{ {built} }} else {{ .done }}\n}}\n\n\
                 fn main() ! console {{\n  match make({more}) {{\n    {matched} {{ print(\"more\") }}\n    .done {{ print(\"done\") }}\n  }}\n}}\n"
            )
        };
        let short = program(".more", ".more");
        let long = program(".more(())", ".more(())");
        let mixed = program(".more(())", ".more");
        let lines = [if more { "more" } else { "done" }.to_string()];
        for source in [&short, &long, &mixed] {
            assert_eq!(run(source), says(&lines), "{source}");
        }
        let (a, b) = (lower(short.as_bytes()), lower(long.as_bytes()));
        assert!(a == b, "the two spellings build different programs");
    });
}

/// `then` is not a function of `Try`, and a `try` in a lambda returns from
/// the lambda, whose result is the one that is split into.
fn try_has_no_then_to_write_and_a_try_in_a_lambda_returns_from_the_lambda() {
    let source = "fn main() ! console, alloc {\n  r: Option[u8] = Try.then(Option.some(1))\n}\n";
    let found = errors(source);
    assert_eq!(found.len(), 1, "{found:?}");
    assert!(found[0].message.contains("`then`"), "{}", found[0]);
    let source = "fn apply(f: fn(u8) -> Option[u8]) -> Option[u8] { f(1) }\n\n\
                  fn go(o: Option[u8]) -> Option[u8] {\n  r = apply() fn(v) {\n    x = try o\n    .some(wrapping_add(x, v))\n  }\n  r\n}\n\n\
                  fn main() ! console, alloc {\n  match go(.some(2)) {\n    .some(v) { print(\"{v}\") }\n    .none { print(\"none\") }\n  }\n  \
                  match go(.none) {\n    .some(v) { print(\"{v}\") }\n    .none { print(\"none\") }\n  }\n}\n";
    assert_eq!(run(source), says(&["3".to_string(), "none".to_string()]), "{source}");
}
