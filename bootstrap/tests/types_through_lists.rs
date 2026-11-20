//! A type that reaches itself only through a `List`, which is how a tree with
//! any number of children is written. A value of one is built, held in a
//! `var`, passed, returned, grown, matched, and read, and the evaluator and
//! every executable the machine can run print what the generator that built
//! the program says. A tree here has a depth fixed by the program: walking one
//! to any depth waits for recursion.

use smith::lower;
use smith::scalar;

use crate::Test;
use crate::generate::{self, Gen};
use crate::targets::{runnable, runs_as};

pub const ONCE: &[Test] = &[
    (
        "a_struct_that_holds_a_list_of_itself_is_built_held_passed_grown_and_read_part_1_of_2",
        || a_struct_that_holds_a_list_of_itself_is_built_held_passed_grown_and_read(0),
    ),
    (
        "a_struct_that_holds_a_list_of_itself_is_built_held_passed_grown_and_read_part_2_of_2",
        || a_struct_that_holds_a_list_of_itself_is_built_held_passed_grown_and_read(1),
    ),
    (
        "an_enum_that_holds_a_list_of_itself_is_built_passed_and_matched",
        an_enum_that_holds_a_list_of_itself_is_built_passed_and_matched,
    ),
    (
        "two_types_that_hold_lists_of_each_other_are_built_and_read",
        two_types_that_hold_lists_of_each_other_are_built_and_read,
    ),
];

/// A program and what it prints.
#[derive(Debug)]
struct Case {
    source: String,
    stdout: String,
}

impl Case {
    /// Checks that the evaluator prints what the generator says, and that
    /// every executable that can run here prints what the evaluator did.
    fn check(&self, targets: &[smith::target::Target]) {
        let program = lower(self.source.as_bytes()).unwrap_or_else(|e| panic!("{e:?}"));
        let said = scalar::run(&scalar::passes(&program));
        assert_eq!(String::from_utf8_lossy(&said.stdout), self.stdout);
        for &target in targets {
            runs_as(self.source.as_bytes(), target, &said);
        }
    }
}

/// A node of a tree the generator builds: its number and its children.
struct Tree {
    value: usize,
    kids: Vec<Tree>,
}

/// A tree of at most `depth` more levels below its root, in which every node
/// has at most three children and its own number.
fn tree(g: &mut Gen, depth: usize, next: &mut usize) -> Tree {
    *next += 1;
    let value = *next + g.below(100);
    let count = if depth == 0 { 0 } else { g.below(4) };
    let kids = (0..count).map(|_| tree(g, depth - 1, next)).collect();
    Tree { value, kids }
}

/// A tree written as the construction of `name`, with `value` and `kids` the
/// names of its fields.
fn written(t: &Tree, name: &str, value: &str, kids: &str) -> String {
    let inner: Vec<String> = t.kids.iter().map(|k| written(k, name, value, kids)).collect();
    format!("{name}({value}: {}, {kids}: [{}])", t.value, inner.join(", "))
}

/// Reads each of the first three children of `xs`, which the program holds in
/// a name so that its size is known to the proof of each index.
fn reads(of: &Tree, xs: &str, out: &mut String) -> String {
    let mut says = String::new();
    for (i, k) in of.kids.iter().take(3).enumerate() {
        out.push_str(&format!(
            "  if {i} < {xs}.size {{\n    print(\"{{{xs}[{i}].value}} {{{xs}[{i}].kids.size}}\")\n  }}\n"
        ));
        says.push_str(&format!("{} {}\n", k.value, k.kids.len()));
    }
    says
}

/// A struct that holds a list of itself, built as a tree of fixed depth,
/// passed and returned through functions that take it, held in a `var`,
/// given one more child, and read: its number, how many children it has, and
/// the number and the children of each of the first three.
fn a_struct_program(g: &mut Gen) -> Case {
    let mut next = 0;
    let depth = 1 + g.below(3);
    let root = tree(g, depth, &mut next);
    let extra_depth = g.below(2);
    let extra = tree(g, extra_depth, &mut next);
    let grow = g.one_in(2);
    let mut source = String::from(
        "use std.console (console, print)\nuse std.list (push)\n\n\
         struct Node {\n  value: Nat\n  kids: List[Node]\n}\n\n\
         fn pass(n: take Node) -> Node { n }\n\n\
         fn grown(n: take Node, extra: take Node) -> Node ! alloc {\n  \
         Node(value: n.value, kids: push(n.kids, extra))\n}\n\n\
         fn main() ! console, alloc {\n",
    );
    source.push_str(&format!(
        "  var held = pass({})\n  held = pass(held)\n",
        written(&root, "Node", "value", "kids")
    ));
    let mut seen = Tree {
        value: root.value,
        kids: Vec::new(),
    };
    for k in root.kids {
        seen.kids.push(k);
    }
    if grow {
        source.push_str(&format!(
            "  held = grown(held, {})\n",
            written(&extra, "Node", "value", "kids")
        ));
        seen.kids.push(extra);
    }
    source.push_str("  print(\"{held.value} {held.kids.size}\")\n  xs = held.kids\n");
    let mut stdout = format!("{} {}\n", seen.value, seen.kids.len());
    stdout.push_str(&reads(&seen, "xs", &mut source));
    source.push_str("}\n");
    Case { source, stdout }
}

fn a_struct_that_holds_a_list_of_itself_is_built_held_passed_grown_and_read(part: u64) {
    let targets = runnable();
    generate::check_part(5880, 24, part, 2, a_struct_program, |case| {
        case.check(&targets);
    });
}

/// An enum with a case that holds a list of itself, built as a tree of fixed
/// depth, passed and returned, and matched: the case it is, and the number or
/// how many it holds.
fn an_enum_program(g: &mut Gen) -> Case {
    /// The source of a tree of the enum, and what the root is: a leaf with its
    /// number, or a forest with how many trees it holds.
    fn forest(g: &mut Gen, depth: usize, next: &mut usize) -> (String, String) {
        *next += 1;
        if depth == 0 || g.one_in(3) {
            return (format!(".leaf({})", *next), format!("leaf {}\n", *next));
        }
        let count = g.below(4);
        let kids: Vec<String> = (0..count).map(|_| forest(g, depth - 1, next).0).collect();
        let says = format!("forest {}\n", kids.len());
        (format!(".forest([{}])", kids.join(", ")), says)
    }
    let mut next = 0;
    let depth = 1 + g.below(3);
    let (root, says) = forest(g, depth, &mut next);
    let source = format!(
        "use std.console (console, print)\n\n\
         enum Tree {{\n  leaf: Nat\n  forest: List[Tree]\n}}\n\n\
         fn pass(t: take Tree) -> Tree {{ t }}\n\n\
         fn main() ! console, alloc {{\n  \
         root: Tree = {root}\n  \
         var held = pass(root)\n  held = pass(held)\n  \
         match held {{\n    \
         .leaf(v) {{ print(\"leaf {{v}}\") }}\n    \
         .forest(xs) {{ print(\"forest {{xs.size}}\") }}\n  }}\n}}\n"
    );
    Case {
        source,
        stdout: says,
    }
}

fn an_enum_that_holds_a_list_of_itself_is_built_passed_and_matched() {
    let targets = runnable();
    generate::check(5881, 12, an_enum_program, |case| case.check(&targets));
}

/// Two structs that each hold a list of the other, built as a tree of fixed
/// depth in which the levels alternate, and read: the number and the member
/// count of the first, and of each of the first three of its members.
fn a_pair_program(g: &mut Gen) -> Case {
    fn level(t: &Tree, band: bool) -> String {
        let inner: Vec<String> = t.kids.iter().map(|k| level(k, !band)).collect();
        match band {
            true => format!("Band(n: {}, members: [{}])", t.value, inner.join(", ")),
            false => format!("Cast(n: {}, bands: [{}])", t.value, inner.join(", ")),
        }
    }
    let mut next = 0;
    let depth = 1 + g.below(3);
    let root = tree(g, depth, &mut next);
    let mut source = String::from(
        "use std.console (console, print)\n\n\
         struct Band {\n  n: Nat\n  members: List[Cast]\n}\n\n\
         struct Cast {\n  n: Nat\n  bands: List[Band]\n}\n\n\
         fn passed(b: take Band) -> Band { b }\n\n\
         fn main() ! console, alloc {\n",
    );
    source.push_str(&format!("  held = passed({})\n", level(&root, true)));
    source.push_str("  print(\"{held.n} {held.members.size}\")\n  xs = held.members\n");
    let mut stdout = format!("{} {}\n", root.value, root.kids.len());
    for (i, k) in root.kids.iter().take(3).enumerate() {
        source.push_str(&format!(
            "  if {i} < xs.size {{\n    print(\"{{xs[{i}].n}} {{xs[{i}].bands.size}}\")\n  }}\n"
        ));
        stdout.push_str(&format!("{} {}\n", k.value, k.kids.len()));
    }
    source.push_str("}\n");
    Case { source, stdout }
}

fn two_types_that_hold_lists_of_each_other_are_built_and_read() {
    let targets = runnable();
    generate::check(5882, 12, a_pair_program, |case| case.check(&targets));
}
