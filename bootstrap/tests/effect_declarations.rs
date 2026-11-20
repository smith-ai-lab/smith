//! An effect a program declares by listing its operations: each operation is
//! a function that carries the effect, so whoever calls it carries it too, an
//! effect and its operations are imported by name, `console` is declared this
//! way in the library, and an effect that reaches `main` with no answer is
//! refused. Where a place is expected, the generator that wrote the program
//! says it.

use smith::diag::{Error, Pos};
use smith::{check, lower_project};

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "a_caller_of_an_operation_carries_its_effect_and_is_refused_naming_the_operation_the_effect_and_itself",
        a_caller_of_an_operation_carries_its_effect_and_is_refused_naming_the_operation_the_effect_and_itself,
    ),
    (
        "a_function_that_calls_an_operation_or_what_carries_the_effect_checks_exactly_when_it_carries_it",
        a_function_that_calls_an_operation_or_what_carries_the_effect_checks_exactly_when_it_carries_it,
    ),
    (
        "an_effect_and_each_operation_of_a_module_are_imported_each_by_its_own_name",
        an_effect_and_each_operation_of_a_module_are_imported_each_by_its_own_name,
    ),
    (
        "console_is_declared_by_the_library_and_print_is_its_operation",
        console_is_declared_by_the_library_and_print_is_its_operation,
    ),
    (
        "an_effect_that_reaches_main_with_no_answer_is_one_error_naming_it",
        an_effect_that_reaches_main_with_no_answer_is_one_error_naming_it,
    ),
];

/// A parameter of an operation: its type, and whether it has a default.
#[derive(Clone, Debug)]
struct Param {
    ty: &'static str,
    default: bool,
}

/// An operation of the effect: its parameters, in which those with a default
/// come last, and the type of its result, if it has one.
#[derive(Clone, Debug)]
struct Op {
    params: Vec<Param>,
    result: Option<&'static str>,
}

/// What a type of a parameter is written as in a call, and as a default.
fn value(ty: &str) -> &'static str {
    match ty {
        "str" => "\"x\"",
        _ => "1",
    }
}

fn effect_of(g: &mut Gen) -> Vec<Op> {
    (0..g.below(3) + 1)
        .map(|_| {
            let count = g.below(4);
            let defaults = g.below(count + 1);
            Op {
                params: (0..count)
                    .map(|i| Param {
                        ty: g.pick(&["i32", "str", "u64"]),
                        default: i >= count - defaults,
                    })
                    .collect(),
                result: [None, Some("i32"), Some("str"), Some("u64")][g.below(4)],
            }
        })
        .collect()
}

/// The text of the declaration of the effect `fx`, with `pub` before it or
/// not.
fn declaration(public: bool, ops: &[Op]) -> String {
    let mut text = String::new();
    text += if public { "pub effect fx {\n" } else { "effect fx {\n" };
    for (k, op) in ops.iter().enumerate() {
        let params: Vec<String> = (op.params.iter().enumerate())
            .map(|(i, p)| match p.default {
                true => format!("a{i}: {} = {}", p.ty, value(p.ty)),
                false => format!("a{i}: {}", p.ty),
            })
            .collect();
        let result = op.result.map_or(String::new(), |r| format!(" -> {r}"));
        // The operations of an effect that other modules use are `pub`.
        let public = if public { "pub " } else { "" };
        text += &format!("  {public}fn op{k}({}){result}\n", params.join(", "));
    }
    text + "}\n"
}

/// A call of the operation `k`, with the parameters that have a default given
/// where `give` says, by name.
fn call(ops: &[Op], k: usize, give: &[bool]) -> String {
    let args: Vec<String> = (ops[k].params.iter().enumerate())
        .filter(|(i, p)| !p.default || give[*i % give.len()])
        .map(|(i, p)| match p.default {
            true => format!("a{i}: {}", value(p.ty)),
            false => value(p.ty).to_string(),
        })
        .collect();
    format!("op{k}({})", args.join(", "))
}

/// The errors of a file, which are none for one that is accepted.
fn errors(source: &str) -> Vec<Error> {
    check(source.as_bytes()).err().unwrap_or_default()
}

/// The place of the first `needle` at or after the line and column given.
fn place(source: &str, needle: &str) -> Pos {
    let at = source.find(needle).unwrap();
    Pos {
        line: source[..at].matches('\n').count() + 1,
        col: at - source[..at].rfind('\n').map_or(0, |n| n + 1) + 1,
    }
}

/// `effect time { fn now() -> u64 }`, and `stamp`, which calls `now`.
fn a_caller_of_an_operation_carries_its_effect_and_is_refused_naming_the_operation_the_effect_and_itself()
{
    let program = |carries: &str, main: &str| {
        format!(
            "use std.bits (u64)\n\neffect time {{\n  fn now() -> u64\n}}\n\n\
             fn stamp() -> u64{carries} {{\n  now()\n}}\n\nfn main(){main} {{\n  x = stamp()\n}}\n"
        )
    };
    // It checks: `main` carries `time` too, and what is said is that nothing
    // answers it, at `main` and not in `stamp`.
    let source = program(" ! time", " ! time");
    let accepted = errors(&source);
    assert_eq!(accepted.len(), 1, "{accepted:?}");
    assert_eq!(accepted[0].pos.line, place(&source, "fn main").line);
    let source = program("", "");
    let refused = errors(&source);
    assert_eq!(refused.len(), 1, "{refused:?}");
    assert_eq!(refused[0].pos, place(&source, "now()\n}\n\nfn main"));
    for name in ["`now`", "`time`", "`stamp`"] {
        assert!(refused[0].message.contains(name), "{name} in {refused:?}");
    }
    assert!(refused[0].fix.as_deref().is_some_and(|f| f.contains("time")));
}

/// A chain of functions, the first of which calls an operation and each other
/// calls the one before. Each carries the effect or not, a function that does
/// not and calls what carries it, or the operation, is refused at the call,
/// and nothing else is refused, with no matter how the operation's parameters,
/// result, and defaults are written, and whether a default is left out.
fn a_function_that_calls_an_operation_or_what_carries_the_effect_checks_exactly_when_it_carries_it()
{
    generate::check(
        5921,
        40,
        |g: &mut Gen| {
            let ops = effect_of(g);
            let k = g.below(ops.len());
            let carries: Vec<bool> = (0..g.below(3) + 1).map(|_| g.below(2) == 0).collect();
            let give: Vec<bool> = (0..3).map(|_| g.below(2) == 0).collect();
            (ops, k, carries, give)
        },
        |(ops, k, carries, give)| {
            let mut source = String::from("use std.bits (i32, u64)\n\n");
            source += &declaration(false, ops);
            let mut expected: Vec<Pos> = Vec::new();
            for (i, carried) in carries.iter().enumerate() {
                let lists = if *carried { " ! fx" } else { "" };
                source += &format!("\nfn f{i}(){lists} {{\n");
                let line = match i {
                    0 => call(ops, *k, give),
                    _ => format!("f{}()", i - 1),
                };
                let at = Pos {
                    line: source.matches('\n').count() + 1,
                    col: 3,
                };
                let bound = i == 0 && ops[*k].result.is_some();
                source += &format!("  {}{line}\n}}\n", if bound { "r = " } else { "" });
                let calls_carrier = i == 0 || carries[i - 1];
                if !carried && calls_carrier {
                    let at = Pos {
                        col: at.col + if bound { 4 } else { 0 },
                        ..at
                    };
                    expected.push(at);
                }
            }
            let top = carries.len() - 1;
            let main = if carries[top] { " ! fx" } else { "" };
            source += &format!("\nfn main(){main} {{\n  f{top}()\n}}\n");
            let mut found: Vec<Error> = errors(&source);
            if carries[top] {
                // `main` carries an effect nothing answers, as the next test
                // looks at, and nothing else is said of the chain.
                let listed = place(&source, "fn main() ! fx");
                let listed = Pos { col: listed.col + 12, ..listed };
                assert!(found.iter().any(|e| e.pos == listed), "{found:?} for\n{source}");
                found.retain(|e| e.pos != listed);
            }
            let mut got: Vec<Pos> = found.iter().map(|e| e.pos).collect();
            got.sort();
            expected.sort();
            assert_eq!(got, expected, "for\n{source}\n{found:?}");
            for e in &found {
                assert!(e.message.contains("`fx`"), "{e:?}");
            }
        },
    );
}

/// An effect of a module is imported by its name, and each operation by its
/// own, and one that is not imported is refused as any name is.
fn an_effect_and_each_operation_of_a_module_are_imported_each_by_its_own_name() {
    generate::check(
        5922,
        40,
        |g: &mut Gen| {
            let ops = effect_of(g);
            (ops, g.below(2) == 0, g.below(2) == 0)
        },
        |(ops, effect, operation)| {
            let module = format!("use std.bits (i32, u64)\n\n{}", declaration(true, ops));
            let names: Vec<&str> = [(*effect, "fx"), (*operation, "op0")]
                .iter()
                .filter(|(listed, _)| *listed)
                .map(|(_, name)| *name)
                .collect();
            let uses = if names.is_empty() {
                String::new()
            } else {
                format!("use app.fx ({})\n\n", names.join(", "))
            };
            let give = [true];
            let source = format!(
                "{uses}fn f() ! fx {{\n  {}{}\n}}\n\nfn main() ! fx {{\n  f()\n}}\n",
                if ops[0].result.is_some() { "r = " } else { "" },
                call(ops, 0, &give)
            );
            let found = lower_project(source.as_bytes(), &[("app.fx", &module)])
                .err()
                .unwrap_or_default();
            // `main` carries an effect nothing answers, which is the one
            // error a file that imports what it uses is given.
            let main = place(&source, "fn main() ! fx");
            let alone = found.len() == 1 && found[0].pos.line == main.line;
            assert_eq!(alone, *effect && *operation, "{found:?} for\n{source}");
        },
    );
}

/// `std/console.smith` declares `console` with `print` as its operation, and
/// holds no other `print`, so a caller of `print` needs `console`.
fn console_is_declared_by_the_library_and_print_is_its_operation() {
    let library = std::fs::read_to_string(
        std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../std/console.smith"),
    )
    .unwrap();
    assert!(library.lines().any(|l| l.starts_with("pub effect console")));
    assert!(!library.contains("__print"));
    assert!(!library.lines().any(|l| l.starts_with("pub fn print")));
    let source = "use std.console (console, print)\n\nfn greet() {\n  print(\"hi\")\n}\n\n\
                  fn main() ! console {\n  greet()\n}\n";
    let refused = errors(source);
    assert_eq!(refused.len(), 1, "{refused:?}");
    assert_eq!(refused[0].pos, place(source, "print(\"hi\")"));
    for name in ["`print`", "`console`", "`greet`"] {
        assert!(refused[0].message.contains(name), "{name} in {refused:?}");
    }
    let program = source.replace("fn greet()", "fn greet() ! console");
    assert!(errors(&program).is_empty(), "{:?}", errors(&program));
}

/// An effect that the program declares and lists in `main`, which the target
/// does not provide and nothing handles, is one error at its name in the
/// list, that names it, and `console` is provided.
fn an_effect_that_reaches_main_with_no_answer_is_one_error_naming_it() {
    generate::check(
        5923,
        30,
        |g: &mut Gen| (g.below(3) + 1, g.below(2) == 0, g.below(2) == 0),
        |(count, with_console, with_alloc)| {
            let mut source = String::from("use std.console (console, print)\n\n");
            let names: Vec<String> = (0..*count).map(|i| format!("fx{i}")).collect();
            for name in &names {
                source += &format!("effect {name} {{\n  fn go_{name}()\n}}\n\n");
            }
            let mut listed = names.clone();
            if *with_console {
                listed.push("console".to_string());
            }
            if *with_alloc {
                listed.push("alloc".to_string());
            }
            source += &format!("fn main() ! {} {{\n}}\n", listed.join(", "));
            let found = errors(&source);
            let mut got: Vec<Pos> = found.iter().map(|e| e.pos).collect();
            got.sort();
            let head = place(&source, "fn main() ! ");
            let mut want: Vec<Pos> = Vec::new();
            let mut col = head.col + "fn main() ! ".len();
            for name in &listed {
                if name.starts_with("fx") {
                    want.push(Pos { col, ..head });
                }
                col += name.len() + 2;
            }
            assert_eq!(got, want, "for\n{source}\n{found:?}");
            for (e, name) in found.iter().zip(&names) {
                assert!(e.message.contains(&format!("`{name}`")), "{e:?}");
            }
        },
    );
}
