//! A lambda that gives away a `var` of the function it is written in and
//! assigns it again before it ends has given nothing away, so the function it
//! is passed to may call it any number of times: this holds exactly when every
//! path out of the lambda assigns the name, a `return` and a `try` that leaves
//! among them, and a handler's function that appends to what its `var` holds is
//! one, and moves no copy of the value. The expectation of each program is what
//! the generator that wrote it says its paths do.

use smith::diag::Error;
use smith::scalar::{self, Outcome};
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::targets;

pub const ONCE: &[Test] = &[
    (
        "a_lambda_that_appends_to_a_var_it_gives_away_is_accepted_and_the_var_is_whole_after_the_call",
        a_lambda_that_appends_to_a_var_it_gives_away_is_accepted_and_the_var_is_whole_after_the_call,
    ),
    (
        "a_lambda_that_gives_away_a_var_is_accepted_exactly_when_every_path_out_of_it_assigns_it",
        a_lambda_that_gives_away_a_var_is_accepted_exactly_when_every_path_out_of_it_assigns_it,
    ),
    (
        "a_try_between_the_give_away_and_the_assignment_is_a_path_that_leaves_with_the_name_given_away",
        a_try_between_the_give_away_and_the_assignment_is_a_path_that_leaves_with_the_name_given_away,
    ),
    (
        "a_handler_that_gathers_a_text_or_pushes_to_a_list_gives_what_was_put_whatever_asks",
        a_handler_that_gathers_a_text_or_pushes_to_a_list_gives_what_was_put_whatever_asks,
    ),
    (
        "an_append_through_a_handler_is_the_instructions_of_the_append_written_in_place",
        an_append_through_a_handler_is_the_instructions_of_the_append_written_in_place,
    ),
];

/// The executables of the programs that gather are checked against the
/// evaluator.
pub const MACHINE: &[Test] = &[(
    "every_executable_of_a_handler_that_gathers_prints_what_the_evaluator_says",
    every_executable_of_a_handler_that_gathers_prints_what_the_evaluator_says,
)];

fn errors(source: &str) -> Vec<Error> {
    check(source.as_bytes()).err().unwrap_or_default()
}

fn outcome(source: &str) -> Outcome {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for\n{source}"));
    scalar::run(&program)
}

/// The function the lambda is passed to: it calls it for "a" and then "b".
const BOTH: &str = "fn both(f: fn(str)) {\n  f(\"a\")\n  f(\"b\")\n}\n\n";

/// The example of `take.md`: `append` takes `page`, and the same statement
/// assigns what it gives back, so `page` is whole after the call.
fn a_lambda_that_appends_to_a_var_it_gives_away_is_accepted_and_the_var_is_whole_after_the_call() {
    let source = format!(
        "use std.console (console, print)\nuse std.text (text, append, finish)\n\n{BOTH}\
         fn gather() -> str ! alloc {{\n  var page = text()\n  both() fn(part) {{ page = append(page, part) }}\n  finish(page)\n}}\n\n\
         fn main() ! console, alloc {{\n  print(gather())\n}}\n"
    );
    assert_eq!(outcome(&source).stdout, b"ab\n");
}

/// What a branch of the lambda does with `page`.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Branch {
    /// Gives `page` away and assigns what comes back.
    Appends,
    /// Gives `page` away to `finish` and assigns a new text.
    Restarts,
    /// Gives `page` away to `finish` and assigns nothing.
    Spends,
}

/// A line of the lambda: a branch on `c`, each side of which does something to
/// `page` and may then leave the lambda with `return`.
#[derive(Clone, Debug)]
struct Line {
    then: (Branch, bool),
    otherwise: (Branch, bool),
}

#[derive(Clone, Debug)]
struct Lambda {
    /// Whether a line is a plain append instead.
    lines: Vec<Option<Line>>,
}

fn branch(g: &mut Gen) -> (Branch, bool) {
    (
        *g.pick(&[Branch::Appends, Branch::Restarts, Branch::Spends]),
        g.below(3) == 2,
    )
}

fn lambda(g: &mut Gen) -> Lambda {
    // A line after one that leaves `page` given away would use a name that is
    // gone, which is another mistake, so the lambda ends at the first line
    // that may leave it so.
    let mut lines = Vec::new();
    for _ in 0..g.below(3) + 1 {
        if g.below(2) == 0 {
            lines.push(None);
        } else {
            let line = Line {
                then: branch(g),
                otherwise: branch(g),
            };
            let spends = |b: &(Branch, bool)| b.0 == Branch::Spends;
            let falls = (spends(&line.then) && !line.then.1)
                || (spends(&line.otherwise) && !line.otherwise.1);
            lines.push(Some(line));
            if falls {
                break;
            }
        }
    }
    Lambda { lines }
}

impl Lambda {
    fn statement(branch: Branch, leaves: bool) -> String {
        let mut text = match branch {
            Branch::Appends => "page = append(page, part)\n".to_string(),
            Branch::Restarts => "s = finish(append(page, part))\npage = text()\n".to_string(),
            Branch::Spends => "s = finish(append(page, part))\n".to_string(),
        };
        if leaves {
            text += "return\n";
        }
        text
    }

    fn body(&self) -> String {
        let mut text = String::new();
        for line in &self.lines {
            match line {
                None => text += "      page = append(page, part)\n",
                Some(l) => {
                    text += "      if c {\n";
                    for row in Lambda::statement(l.then.0, l.then.1).lines() {
                        text += &format!("        {row}\n");
                    }
                    text += "      } else {\n";
                    for row in Lambda::statement(l.otherwise.0, l.otherwise.1).lines() {
                        text += &format!("        {row}\n");
                    }
                    text += "      }\n";
                }
            }
        }
        text
    }

    /// Whether every path out of the lambda has `page`: the paths that leave
    /// by `return` and the one that reaches the end.
    fn assigns_on_every_path(&self) -> bool {
        for line in self.lines.iter().flatten() {
            for (branch, leaves) in [line.then, line.otherwise] {
                if leaves && branch == Branch::Spends {
                    return false;
                }
            }
        }
        // The path that reaches the end has given `page` away when a branch
        // that does not leave spends it.
        !self.lines.iter().flatten().any(|l| {
            [l.then, l.otherwise]
                .iter()
                .any(|(b, leaves)| *b == Branch::Spends && !leaves)
        })
    }

    /// What `page` holds after the lambda was called for each of the parts, when
    /// the condition is `c`, for a lambda every path of which assigns.
    fn gathers(&self, c: bool) -> String {
        let mut page = String::new();
        for part in ["a", "b"] {
            'call: for line in &self.lines {
                match line {
                    None => page += part,
                    Some(l) => {
                        let (branch, leaves) = if c { l.then } else { l.otherwise };
                        match branch {
                            Branch::Appends => page += part,
                            Branch::Restarts => page.clear(),
                            Branch::Spends => unreachable!("every path assigns"),
                        }
                        if leaves {
                            break 'call;
                        }
                    }
                }
            }
        }
        page
    }

    fn source(&self, c: bool) -> String {
        format!(
            "use std.console (console, print)\nuse std.text (text, append, finish)\n\n{BOTH}\
             fn gather(c: bool) -> str ! alloc {{\n  var page = text()\n  both() fn(part) {{\n{}  }}\n  finish(page)\n}}\n\n\
             fn main() ! console, alloc {{\n  print(gather({c}))\n}}\n",
            self.body()
        )
    }
}

/// The checker accepts a lambda exactly when every path out of it assigns the
/// name it gave away; one that does not is refused with the error of a lambda
/// that gives something away, naming `page`, and one that does runs and gives
/// what the paths say.
fn a_lambda_that_gives_away_a_var_is_accepted_exactly_when_every_path_out_of_it_assigns_it() {
    generate::check(7601, 60, lambda, |l| {
        let source = l.source(true);
        let found = errors(&source);
        if l.assigns_on_every_path() {
            assert!(found.is_empty(), "{found:?} for\n{source}");
            for c in [true, false] {
                assert_eq!(
                    outcome(&l.source(c)).stdout,
                    format!("{}\n", l.gathers(c)).into_bytes(),
                    "for\n{}",
                    l.source(c)
                );
            }
        } else {
            assert!(
                found.iter().any(|e| e.message.contains("gives away `page`")),
                "{found:?} for\n{source}"
            );
        }
    });
}

/// A `try` between the give-away and the assignment can leave with the name
/// given away, so it is refused; the same `try` before the give-away, or after
/// the assignment, leaves nothing given away and is accepted.
fn a_try_between_the_give_away_and_the_assignment_is_a_path_that_leaves_with_the_name_given_away() {
    let program = |lines: &str| {
        format!(
            "use std.console (console, print)\nuse std.text (text, append, finish)\n\n\
             fn both(f: fn(str) -> Result[(), str]) -> Result[(), str] {{\n  try f(\"a\")\n  f(\"b\")\n}}\n\n\
             fn ok(s: str) -> Result[(), str] {{ .ok(()) }}\n\n\
             fn gather() -> Result[str, str] ! alloc {{\n  var page = text()\n  r = both() fn(part) {{\n{lines}    .ok(())\n  }}\n  try r\n  .ok(finish(page))\n}}\n\n\
             fn main() ! console, alloc {{\n  r = gather()\n  print(\"x\")\n}}\n"
        )
    };
    let between = program("    t = append(page, part)\n    try ok(part)\n    page = t\n");
    let found = errors(&between);
    assert!(
        found.iter().any(|e| e.message.contains("gives away `page`")),
        "{found:?} for\n{between}"
    );
    let before = program("    try ok(part)\n    page = append(page, part)\n");
    assert!(errors(&before).is_empty(), "{:?}", errors(&before));
    let after = program("    page = append(page, part)\n    try ok(part)\n");
    assert!(errors(&after).is_empty(), "{:?}", errors(&after));
}

/// What a handler is asked to keep, as the calls that put it.
#[derive(Clone, Debug)]
struct Puts {
    /// Whether the handler gathers a `Text`, or pushes to a `List[Byte]`.
    text: bool,
    /// What is put directly under the handler, then by a function that carries
    /// the effect, then directly again.
    parts: [Vec<u8>; 3],
}

fn puts(g: &mut Gen) -> Puts {
    let part = |g: &mut Gen| (0..g.below(3)).map(|_| b'a' + g.below(26) as u8).collect();
    Puts {
        text: g.below(2) == 0,
        parts: [part(g), part(g), part(g)],
    }
}

impl Puts {
    fn put(&self, bytes: &[u8]) -> String {
        bytes
            .iter()
            .map(|b| match self.text {
                true => format!("    put(\"{}\")\n", *b as char),
                false => format!("    put({b})\n"),
            })
            .collect()
    }

    /// The program: a handler that keeps what is put in a `var`, a function that
    /// puts, and a function that runs both under the handler.
    fn source(&self, in_place: bool) -> String {
        let (ty, param, arg, start, add, end) = match self.text {
            true => (
                "str",
                "part: str",
                "part",
                "text()",
                "append(held, part)",
                "finish(held)",
            ),
            false => ("Byte", "b: Byte", "b", "[]", "push(held, b)", "held"),
        };
        let result = if self.text { "str" } else { "List[Byte]" };
        let uses = if self.text {
            "use std.text (text, append, finish)"
        } else {
            "use std.list (push)"
        };
        let mut text = format!("use std.console (console, print)\n{uses}\n\n");
        if in_place {
            // Everything written where it is asked, with no handler.
            let mut all = String::new();
            let var = if self.text { "var held = text()" } else { "var held: List[Byte] = []" };
            all += &format!("fn run() -> {result} ! alloc {{\n  {var}\n");
            for part in &self.parts {
                for b in part {
                    let value = if self.text { format!("\"{}\"", *b as char) } else { b.to_string() };
                    let step = if self.text {
                        format!("append(held, {value})")
                    } else {
                        format!("push(held, {value})")
                    };
                    all += &format!("  held = {step}\n");
                }
            }
            all += &format!("  {end}\n}}\n\n");
            text += &all;
        } else {
            text += &format!(
                "effect sink {{\n  fn put({param})\n}}\n\n\
                 fn gather[T](body: fn() -> T ! sink) -> (T, {result}) ! alloc {{\n  var held{} = {start}\n  \
                 r = sink(put: fn({arg}) {{ held = {add} }}, body: body)\n  (r, {end})\n}}\n\n\
                 fn middle() ! sink {{\n{}}}\n\n\
                 fn run() -> {result} ! alloc {{\n  (n, kept) = gather() {{\n{}    middle()\n{}  }}\n  kept\n}}\n\n",
                if self.text { String::new() } else { format!(": List[{ty}]") },
                self.put(&self.parts[1]),
                self.put(&self.parts[0]),
                self.put(&self.parts[2]),
            );
        }
        text + "fn main() ! console, alloc {\n  print(\"{run()}\")\n}\n"
    }

    fn prints(&self) -> Vec<u8> {
        let all: Vec<u8> = self.parts.iter().flatten().copied().collect();
        if self.text {
            let mut said = all;
            said.push(b'\n');
            said
        } else {
            let items: Vec<String> = all.iter().map(|b| b.to_string()).collect();
            format!("[{}]\n", items.join(", ")).into_bytes()
        }
    }
}

/// A handler's function that gives away what its `var` holds and assigns it
/// again is accepted, builds, and gives what was put, whether the code that puts
/// is under `with` or in a function that carries the effect.
fn a_handler_that_gathers_a_text_or_pushes_to_a_list_gives_what_was_put_whatever_asks() {
    generate::check(7602, 40, puts, |p| {
        let source = p.source(false);
        assert_eq!(outcome(&source).stdout, p.prints(), "for\n{source}");
    });
}

/// The two programs, a handler and the same puts written where they are asked,
/// are the same after the passes: the value is passed and given back, and no
/// copy of it is made, so the instructions are the same but for the calls of the
/// copy of the function that carries the effect.
fn an_append_through_a_handler_is_the_instructions_of_the_append_written_in_place() {
    let shape = |source: &str| {
        let built = scalar::passes(&lower(source.as_bytes()).expect("a program"));
        let instructions: usize = (built.functions.iter())
            .flat_map(|f| &f.blocks)
            .map(|b| b.instructions.len())
            .sum();
        (built.functions.len(), instructions)
    };
    generate::check(7603, 30, puts, |p| {
        assert_eq!(
            shape(&p.source(false)),
            shape(&p.source(true)),
            "for\n{}\nand\n{}",
            p.source(false),
            p.source(true)
        );
    });
}

/// Every executable that can run here prints what the evaluator says.
fn every_executable_of_a_handler_that_gathers_prints_what_the_evaluator_says() {
    generate::check(7604, 6, puts, |p| {
        let source = p.source(false);
        let said = outcome(&source);
        for target in targets::runnable() {
            targets::runs_as(source.as_bytes(), target, &said);
        }
    });
}
