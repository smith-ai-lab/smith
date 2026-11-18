//! Generators and properties of `List`: built from a literal and `push`,
//! read by an index that `arith` proves, and a value like any other.
//!
//! Every generator keeps a model of the lists it builds, in plain vectors, so
//! it shares nothing with the compiler.

use smith::scalar::{self, Op, handler};
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::numbers::{INTS, Int, value};

/// The bytes before a list's elements that say how many of them are in use,
/// in every request for memory that holds a list.
const WORD: usize = 8;

pub const ONCE: &[Test] = &[
    (
        "a_program_builds_a_list_pushes_to_it_and_reads_each_element",
        a_program_builds_a_list_pushes_to_it_and_reads_each_element,
    ),
    (
        "a_list_holds_the_size_and_the_elements_that_were_put_in_in_order",
        a_list_holds_the_size_and_the_elements_that_were_put_in_in_order,
    ),
    (
        "an_index_compiles_exactly_when_arith_proves_it_below_the_size",
        an_index_compiles_exactly_when_arith_proves_it_below_the_size,
    ),
    (
        "a_var_list_and_a_var_field_are_known_to_arith_until_they_are_assigned",
        a_var_list_and_a_var_field_are_known_to_arith_until_they_are_assigned,
    ),
    (
        "an_index_is_never_tested_when_the_program_runs",
        an_index_is_never_tested_when_the_program_runs,
    ),
    (
        "a_name_given_a_list_holds_the_same_elements_after_any_other_name_is_pushed_to",
        a_name_given_a_list_holds_the_same_elements_after_any_other_name_is_pushed_to,
    ),
    (
        "building_a_list_by_pushing_asks_for_memory_a_logarithmic_number_of_times",
        building_a_list_by_pushing_asks_for_memory_a_logarithmic_number_of_times,
    ),
    (
        "a_list_has_room_for_at_most_twice_its_elements_each_at_the_size_of_its_type",
        a_list_has_room_for_at_most_twice_its_elements_each_at_the_size_of_its_type,
    ),
    (
        "push_and_a_list_with_elements_need_the_alloc_effect",
        push_and_a_list_with_elements_need_the_alloc_effect,
    ),
    (
        "a_list_of_people_prints_each_name_and_age",
        a_list_of_people_prints_each_name_and_age,
    ),
    (
        "a_list_of_any_type_holds_what_was_put_in_in_order",
        a_list_of_any_type_holds_what_was_put_in_in_order,
    ),
    (
        "a_list_held_inside_another_value_is_unchanged_by_a_push_to_any_other_name",
        a_list_held_inside_another_value_is_unchanged_by_a_push_to_any_other_name,
    ),
    (
        "a_push_to_a_list_held_inside_another_value_grows_in_place_while_it_has_room",
        a_push_to_a_list_held_inside_another_value_grows_in_place_while_it_has_room,
    ),
    (
        "a_list_that_a_function_takes_in_a_struct_and_returns_grows_as_a_plain_var_does",
        a_list_that_a_function_takes_in_a_struct_and_returns_grows_as_a_plain_var_does,
    ),
    (
        "a_list_that_is_a_field_of_a_var_grows_as_a_plain_var_does",
        a_list_that_is_a_field_of_a_var_grows_as_a_plain_var_does,
    ),
    (
        "a_list_that_a_function_pushes_to_and_returns_grows_as_a_plain_var_does",
        a_list_that_a_function_pushes_to_and_returns_grows_as_a_plain_var_does,
    ),
    (
        "an_element_takes_its_scalars_at_their_sizes_and_a_whole_number_of_its_largest",
        an_element_takes_its_scalars_at_their_sizes_and_a_whole_number_of_its_largest,
    ),
    (
        "reading_a_field_of_an_element_reads_only_the_scalars_of_that_field",
        reading_a_field_of_an_element_reads_only_the_scalars_of_that_field,
    ),
    (
        "a_loop_over_a_list_sums_it",
        a_loop_over_a_list_sums_it,
    ),
    (
        "a_loop_over_a_list_of_any_type_gives_each_element_of_the_list_it_began_with_until_a_break",
        a_loop_over_a_list_of_any_type_gives_each_element_of_the_list_it_began_with_until_a_break,
    ),
    (
        "a_loop_over_a_list_is_one_comparison_and_one_load_of_the_element_each_time_round",
        a_loop_over_a_list_is_one_comparison_and_one_load_of_the_element_each_time_round,
    ),
    (
        "a_loop_runs_over_a_range_or_a_list_and_nothing_else",
        a_loop_runs_over_a_range_or_a_list_and_nothing_else,
    ),
];

fn prints(source: &str, lines: Vec<String>) {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let stdout: String = lines.iter().map(|l| format!("{l}\n")).collect();
    assert_eq!(
        scalar::run(&program),
        scalar::Outcome {
            stdout: stdout.into_bytes(),
            status: 0
        },
        "for {source}"
    );
}

/// The messages of the errors a program is refused with, or none if it
/// compiles.
fn refused(source: &str) -> Vec<String> {
    match check(source.as_bytes()) {
        Ok(_) => Vec::new(),
        Err(errors) => errors.into_iter().map(|e| e.message).collect(),
    }
}

/// The type of the elements of a list: a whole number type, or `bool`.
#[derive(Clone, Copy, Debug)]
enum Elem {
    Int(Int),
    Bool,
}

/// An element, as the number it is or the truth it is.
#[derive(Clone, Copy, Debug)]
enum Val {
    N(i128),
    B(bool),
}

impl Val {
    fn written(self) -> String {
        match self {
            Val::N(n) => n.to_string(),
            Val::B(b) => b.to_string(),
        }
    }
}

impl Elem {
    fn name(self) -> &'static str {
        match self {
            Elem::Int(t) => t.name,
            Elem::Bool => "bool",
        }
    }

    /// How many bytes an element of the type takes.
    fn width(self) -> usize {
        match self {
            Elem::Int(t) => t.bits as usize / 8,
            Elem::Bool => 1,
        }
    }

    fn value(self, g: &mut Gen) -> Val {
        match self {
            Elem::Int(t) => Val::N(value(g, t)),
            Elem::Bool => Val::B(g.one_in(2)),
        }
    }

    /// Statements that write the elements of the list `name`, one to a line
    /// after its size, in a function that may print.
    fn dump(self, name: &str) -> String {
        let element = match self {
            Elem::Int(_) => format!("print(\"{{{name}[i]}}\")"),
            Elem::Bool => format!("if {name}[i] {{ print(\"true\") }} else {{ print(\"false\") }}"),
        };
        format!(
            "  print(\"{{{name}.size}}\")\n  loop i in 0..{name}.size {{\n    {element}\n  }}\n"
        )
    }
}

fn elem(g: &mut Gen) -> Elem {
    if g.below(9) == 0 {
        Elem::Bool
    } else {
        Elem::Int(*g.pick(INTS))
    }
}

/// What the lines of `dump` print for a list.
fn shown(items: &[Val]) -> Vec<String> {
    let mut lines = vec![items.len().to_string()];
    lines.extend(items.iter().map(|v| v.written()));
    lines
}

fn literal(items: &[Val]) -> String {
    let items: Vec<String> = items.iter().map(|v| v.written()).collect();
    format!("[{}]", items.join(", "))
}

#[derive(Debug)]
struct Build {
    elem: Elem,
    start: Vec<Val>,
    pushes: Vec<Val>,
    /// Whether the list is a `var` pushed to in place, or a name for each
    /// list that `push` makes of the last.
    var: bool,
}

fn build(g: &mut Gen) -> Build {
    let elem = elem(g);
    let start = (0..g.below(4)).map(|_| elem.value(g)).collect();
    let pushes = (0..g.below(9)).map(|_| elem.value(g)).collect();
    Build {
        elem,
        start,
        pushes,
        var: g.one_in(2),
    }
}

fn a_program_builds_a_list_pushes_to_it_and_reads_each_element() {
    let source = "fn main() ! console, alloc {\n  var xs: List[u8] = [1, 2, 3]\n  xs = push(xs, 4)\n  ys = xs\n  loop i in 0..ys.size { print(\"{ys[i]}\") }\n}\n";
    prints(source, ["1", "2", "3", "4"].map(String::from).to_vec());
}

/// For every type of whole number and `bool`, and every list built from a
/// literal, or from `[]`, and a run of pushes, whether to a `var` or one name
/// after another, the size and each element are what was put in, in order.
fn a_list_holds_the_size_and_the_elements_that_were_put_in_in_order() {
    generate::check(61, 200, build, |b| {
        let t = b.elem.name();
        let mut body = String::new();
        let last = if b.var {
            body += &format!("  var xs: List[{t}] = {}\n", literal(&b.start));
            for v in &b.pushes {
                body += &format!("  xs = push(xs, {})\n", v.written());
            }
            body += "  ys = xs\n";
            "ys".to_string()
        } else {
            body += &format!("  l0: List[{t}] = {}\n", literal(&b.start));
            for (k, v) in b.pushes.iter().enumerate() {
                body += &format!("  l{} = push(l{k}, {})\n", k + 1, v.written());
            }
            format!("l{}", b.pushes.len())
        };
        body += &b.elem.dump(&last);
        let source = format!("fn main() ! console, alloc {{\n{body}}}\n");
        let all: Vec<Val> = b.start.iter().chain(&b.pushes).copied().collect();
        prints(&source, shown(&all));
    });
}

#[derive(Debug)]
struct Reach {
    /// How many elements the list has.
    n: usize,
    /// The index, or how many numbers the loop runs over.
    j: usize,
    shape: usize,
}

fn reach(g: &mut Gen) -> Reach {
    let n = g.below(5);
    let j = if g.one_in(2) { n } else { g.below(7) };
    Reach {
        n,
        j,
        shape: g.below(5),
    }
}

/// A list that is a `var`, and a field of a `var`, are known to `arith` as any
/// value is, and everything known of one is forgotten at an assignment to it
/// or to a field of it, however the assignment is reached.
fn a_var_list_and_a_var_field_are_known_to_arith_until_they_are_assigned() {
    let list = |body: &str| {
        format!(
            "fn run(f: fn()) {{ f() }}\n\nfn main() ! console, alloc {{\n  var xs: List[u8] = [1, 2, 3]\n  k: Nat = 1\n  if k < xs.size {{\n{body}\n  }}\n}}\n"
        )
    };
    let field = |body: &str| {
        format!(
            "struct Cursor {{\n  at: u64\n}}\n\nfn main() ! console, alloc {{\n  var c = Cursor(at: 1)\n  n: u64 = 5\n  if c.at < n {{\n{body}\n  }}\n}}\n"
        )
    };
    let read = "    print(\"{xs[k]}\")";
    let kept = [
        list(read),
        list("    if k == 7 { xs = [] } else { print(\"{xs[k]}\") }"),
        field("    print(\"{c.at + 1}\")"),
    ];
    for source in &kept {
        assert_eq!(refused(source), Vec::<String>::new(), "for {source}");
    }
    let lost = [
        list(&format!("    xs = push(xs, 4)\n{read}")),
        list(&format!(
            "    loop i in 0..2 {{ xs = push(xs, 4) }}\n{read}"
        )),
        list(&format!(
            "    loop i in 0..2 {{\n      {read}\n      xs = push(xs, 4)\n    }}"
        )),
        list(&format!("    if k == 7 {{ xs = push(xs, 4) }}\n{read}")),
        list(&format!("    run({{ xs = push(xs, 4) }})\n{read}")),
        list(&format!(
            "    run({{\n      xs = push(xs, 4)\n      {read}\n    }})"
        )),
        field("    c.at = 18446744073709551615\n    print(\"{c.at + 1}\")"),
        field("    c = Cursor(at: 18446744073709551615)\n    print(\"{c.at + 1}\")"),
    ];
    for source in &lost {
        assert!(!refused(source).is_empty(), "compiles: {source}");
    }
    let example = "fn main() ! console, alloc {\n  var xs: List[u8] = [1, 2, 3]\n  xs = push(xs, 4)\n  loop i in 0..xs.size { if i == 3 { print(\"{xs[i]}\") } }\n}\n";
    prints(example, vec!["4".to_string()]);
}

/// `xs[i]` compiles exactly when `arith` proves `i < xs.size` from what is in
/// scope: the size of a list written out, the range of a `loop`, an `if`,
/// and a `requires`. When it cannot, the error states the goal.
fn an_index_compiles_exactly_when_arith_proves_it_below_the_size() {
    generate::check(67, 150, reach, |r| {
        let items = vec!["7"; r.n].join(", ");
        let (body, proven, goal) = match r.shape {
            0 => (
                format!("  xs: List[u8] = [{items}]\n  print(\"{{xs[{}]}}\")\n", r.j),
                r.j < r.n,
                format!("{} < xs.size", r.j),
            ),
            1 => (
                format!(
                    "  xs: List[u8] = [{items}]\n  loop i in 0..{} {{ print(\"{{xs[i]}}\") }}\n",
                    r.j
                ),
                r.j <= r.n,
                "i < xs.size".to_string(),
            ),
            2 => (
                format!(
                    "  xs: List[u8] = [{items}]\n  k: Nat = {}\n  if k < xs.size {{ print(\"{{xs[k]}}\") }}\n",
                    r.j
                ),
                true,
                "k < xs.size".to_string(),
            ),
            3 => (
                format!(
                    "  xs: List[u8] = [{items}]\n  k: Nat = {}\n  print(\"{{xs[k]}}\")\n",
                    r.j
                ),
                r.j < r.n,
                "k < xs.size".to_string(),
            ),
            _ => (
                format!(
                    "  xs: List[u8] = [{items}]\n  print(\"{{at(xs, {})}}\")\n",
                    r.j
                ),
                r.j < r.n,
                format!("{} < xs.size", r.j),
            ),
        };
        let source = format!(
            "fn at(xs: List[u8], k: Nat) -> u8\n  requires k < xs.size\n{{\n  xs[k]\n}}\n\nfn main() ! console, alloc {{\n{body}}}\n"
        );
        let errors = refused(&source);
        assert_eq!(errors.is_empty(), proven, "{errors:?} for {source}");
        if proven {
            let lines = match r.shape {
                1 => vec!["7".to_string(); r.j],
                2 => vec!["7".to_string(); usize::from(r.j < r.n)],
                _ => vec!["7".to_string()],
            };
            prints(&source, lines);
        } else {
            assert!(
                errors.iter().any(|e| e.contains(&goal)),
                "{errors:?} does not state `{goal}` for {source}"
            );
        }
    });
}

/// Reading each element of a list in a loop is the loop's one comparison and
/// one load, with nothing that tests the index.
fn an_index_is_never_tested_when_the_program_runs() {
    for ty in INTS {
        let t = ty.name;
        let source = format!(
            "fn total(xs: List[{t}]) -> {t} {{\n  var sum: {t} = 0\n  loop i in 0..xs.size {{ sum = wrapping_add(sum, xs[i]) }}\n  sum\n}}\n\nfn main() ! alloc {{\n  xs: List[{t}] = [1, 2, 3]\n  total(xs)\n}}\n"
        );
        // The function that sums, before inlining puts it into `main`, where
        // nothing reads the sum.
        let program = lower(source.as_bytes()).expect("compiles");
        let (mut compares, mut loads) = (0, 0);
        for f in program
            .functions
            .iter()
            .enumerate()
            .filter(|(at, f)| *at != program.main && !handler::takes_room(&program, f))
            .map(|(_, f)| f)
        {
            for block in &f.blocks {
                for instruction in &block.instructions {
                    match &instruction.op {
                        Op::Compare { .. } => compares += 1,
                        Op::Load { .. } => loads += 1,
                        _ => {}
                    }
                }
            }
        }
        assert_eq!((compares, loads), (1, 1), "for {source}");
    }
}

/// A program that builds lists with `var`s and snapshots, whose model says
/// what each name holds at the end.
#[derive(Debug)]
struct Names {
    source: String,
    lines: Vec<String>,
}

fn names(g: &mut Gen) -> Names {
    let mut vars: Vec<Vec<u8>> = Vec::new();
    let mut snaps: Vec<Vec<u8>> = Vec::new();
    let mut body = String::new();
    let byte = |g: &mut Gen| g.below(256) as u8;
    for _ in 0..1 + g.below(14) {
        let step = if vars.is_empty() { 0 } else { g.below(14) };
        let j = if vars.is_empty() {
            0
        } else {
            g.below(vars.len())
        };
        let k = if vars.is_empty() {
            0
        } else {
            g.below(vars.len())
        };
        match step {
            0 | 13 => {
                let items: Vec<u8> = (0..g.below(4)).map(|_| byte(g)).collect();
                let shown: Vec<String> = items.iter().map(u8::to_string).collect();
                body += &format!("  var a{}: List[u8] = [{}]\n", vars.len(), shown.join(", "));
                vars.push(items);
            }
            1 => {
                body += &format!("  n{} = a{j}\n", snaps.len());
                snaps.push(vars[j].clone());
            }
            2 | 3 => {
                let v = byte(g);
                body += &format!("  a{j} = push(a{j}, {v})\n");
                vars[j].push(v);
            }
            4 => {
                body += &format!("  var a{} = a{j}\n", vars.len());
                let copy = vars[j].clone();
                vars.push(copy);
            }
            5 => {
                body += &format!("  a{k} = a{j}\n");
                vars[k] = vars[j].clone();
            }
            6 => {
                let v = byte(g);
                body += &format!("  n{} = push(a{j}, {v})\n", snaps.len());
                let mut copy = vars[j].clone();
                copy.push(v);
                snaps.push(copy);
            }
            7 if !snaps.is_empty() => {
                let s = g.below(snaps.len());
                let v = byte(g);
                body += &format!("  a{k} = push(n{s}, {v})\n");
                vars[k] = snaps[s].clone();
                vars[k].push(v);
            }
            8 if !snaps.is_empty() => {
                let s = g.below(snaps.len());
                body += &format!("  a{k} = n{s}\n");
                vars[k] = snaps[s].clone();
            }
            9 => {
                body += &format!("  n{} = grow(a{j})\n", snaps.len());
                let mut copy = vars[j].clone();
                copy.push(9);
                snaps.push(copy);
            }
            10 => {
                body += &format!("  a{k} = grow(a{j})\n");
                let mut copy = vars[j].clone();
                copy.push(9);
                vars[k] = copy;
            }
            11 => {
                let v = byte(g);
                body += &format!("  loop i in 0..3 {{ a{j} = push(a{j}, {v}) }}\n");
                vars[j].extend([v; 3]);
            }
            _ => {
                let v = byte(g);
                body += &format!("  a{j} = push(a{j}, {v})\n");
                vars[j].push(v);
            }
        }
    }
    let dump = Elem::Int(INTS[1]);
    let mut lines = Vec::new();
    for (k, items) in snaps.iter().enumerate() {
        body += &dump.dump(&format!("n{k}"));
        lines.extend(shown(
            &items
                .iter()
                .map(|b| Val::N(i128::from(*b)))
                .collect::<Vec<_>>(),
        ));
    }
    for (k, items) in vars.iter().enumerate() {
        body += &format!("  f{k} = a{k}\n");
        body += &dump.dump(&format!("f{k}"));
        lines.extend(shown(
            &items
                .iter()
                .map(|b| Val::N(i128::from(*b)))
                .collect::<Vec<_>>(),
        ));
    }
    let source = format!(
        "fn grow(xs: List[u8]) -> List[u8] ! alloc {{\n  var grown = xs\n  grown = push(grown, 9)\n  grown\n}}\n\nfn main() ! console, alloc {{\n{body}}}\n"
    );
    Names { source, lines }
}

/// A name given a list holds the same elements after any other name is
/// pushed to, whether the two names were given the same list, one a `var`
/// given the other, a `var` given a snapshot, or a copy `push` made. Lists
/// that share a beginning are built in every order of binding and pushing.
fn a_name_given_a_list_holds_the_same_elements_after_any_other_name_is_pushed_to() {
    generate::check(71, 300, names, |n| prints(&n.source, n.lines.clone()));
}

fn ceil_log2(n: usize) -> usize {
    (usize::BITS - (n - 1).leading_zeros()) as usize
}

/// A program that pushes `n` times to one `var` that starts empty, and does
/// nothing else, so that the last memory it asked for is the list's.
fn pushed(elem: Elem, n: usize) -> String {
    let one = match elem {
        Elem::Int(_) => "1",
        Elem::Bool => "true",
    };
    format!(
        "fn main() ! alloc {{\n  var xs: List[{}] = []\n  loop i in 0..{n} {{ xs = push(xs, {one}) }}\n}}\n",
        elem.name()
    )
}

fn sizes_of() -> impl Iterator<Item = (Elem, usize)> {
    let elems: Vec<Elem> = INTS
        .iter()
        .map(|t| Elem::Int(*t))
        .chain([Elem::Bool])
        .collect();
    let ns = [1, 2, 3, 4, 5, 8, 9, 100, 1000];
    elems
        .into_iter()
        .flat_map(move |e| ns.into_iter().map(move |n| (e, n)))
}

/// Building a list of `n` elements by pushing to one `var` asks the system
/// for memory a number of times that grows as the logarithm of `n`.
fn building_a_list_by_pushing_asks_for_memory_a_logarithmic_number_of_times() {
    for (elem, n) in sizes_of() {
        let source = pushed(elem, n);
        let program = lower(source.as_bytes()).expect("compiles");
        let (outcome, requests) = scalar::run_taking(&program, usize::MAX);
        assert_eq!(outcome.status, 0, "for {source}");
        assert!(
            requests.len() <= 1 + ceil_log2(n),
            "{} requests for {source}",
            requests.len()
        );
    }
}

/// A list of `n` elements has room for at most `2 * n` of them, each at the
/// size of its type, so a `List[u8]` has room for at most `2 * n` bytes.
fn a_list_has_room_for_at_most_twice_its_elements_each_at_the_size_of_its_type() {
    for (elem, n) in sizes_of() {
        let source = pushed(elem, n);
        let program = lower(source.as_bytes()).expect("compiles");
        let (_, requests) = scalar::run_taking(&program, usize::MAX);
        let room = *requests.last().expect("memory was asked for") - WORD;
        assert!(room >= n * elem.width(), "{room} bytes for {source}");
        assert!(room <= 2 * n * elem.width(), "{room} bytes for {source}");
    }
}

fn push_and_a_list_with_elements_need_the_alloc_effect() {
    let pushes = "fn main() {\n  xs: List[u8] = []\n  ys = push(xs, 1)\n}\n";
    let literal = "fn main() {\n  xs: List[u8] = [1]\n}\n";
    for source in [pushes, literal] {
        let errors = refused(source);
        assert!(
            errors.iter().any(|e| e.contains("`alloc`")),
            "{errors:?} does not name `alloc` for {source}"
        );
    }
    let empty = "fn main() {\n  xs: List[u8] = []\n  ys = xs.size\n}\n";
    assert_eq!(refused(empty), Vec::<String>::new());
}

fn a_list_of_people_prints_each_name_and_age() {
    let source = "struct Person {\n  name: str\n  age: u8\n}\n\nfn main() ! console, alloc {\n  var people: List[Person] = []\n  people = push(people, Person(name: \"ada\", age: 36))\n  people = push(people, Person(name: \"alan\", age: 41))\n  all = people\n  loop i in 0..all.size { print(\"{all[i].name} {all[i].age}\") }\n}\n";
    prints(source, ["ada 36", "alan 41"].map(String::from).to_vec());
}

/// A type a list can hold, as the generator builds it.
#[derive(Clone, Debug)]
enum Ty {
    Int(Int),
    Bool,
    Str,
    /// A struct, by its number, with the type of each field.
    Struct(usize, Vec<Ty>),
    /// An enum, by its number, with the type each case holds, if it holds one.
    Enum(usize, Vec<Option<Ty>>),
    Tuple(Vec<Ty>),
    List(Box<Ty>),
}

/// What kinds of type the generator may build.
#[derive(Clone, Copy)]
struct Kinds {
    enums: bool,
    lists: bool,
}

const EVERY: Kinds = Kinds {
    enums: true,
    lists: true,
};

/// A value of a type, which says what a program that builds it prints.
#[derive(Clone, Debug)]
enum V {
    N(i128),
    B(bool),
    S(String),
    /// The parts of a struct or a tuple.
    Parts(Vec<V>),
    Case(usize, Option<Box<V>>),
    L(Vec<V>),
}

/// A type, with `depth` more levels of types that hold types.
fn ty(g: &mut Gen, depth: usize, kinds: Kinds, next: &mut usize) -> Ty {
    let leaf = |g: &mut Gen| match g.below(5) {
        0 => Ty::Bool,
        1 => Ty::Str,
        _ => Ty::Int(*g.pick(INTS)),
    };
    if depth == 0 {
        return leaf(g);
    }
    match g.below(8) {
        0..=2 => leaf(g),
        3 | 4 => {
            let id = *next;
            *next += 1;
            let fields = (0..1 + g.below(3))
                .map(|_| ty(g, depth - 1, kinds, next))
                .collect();
            Ty::Struct(id, fields)
        }
        5 if kinds.enums => {
            let id = *next;
            *next += 1;
            let mut cases = vec![None];
            for _ in 0..1 + g.below(2) {
                cases.push(Some(ty(g, depth - 1, kinds, next)));
            }
            Ty::Enum(id, cases)
        }
        6 => Ty::Tuple(
            (0..2 + g.below(2))
                .map(|_| ty(g, depth - 1, kinds, next))
                .collect(),
        ),
        _ if kinds.lists => Ty::List(Box::new(ty(g, depth - 1, kinds, next))),
        _ => leaf(g),
    }
}

impl Ty {
    fn name(&self) -> String {
        match self {
            Ty::Int(t) => t.name.to_string(),
            Ty::Bool => "bool".to_string(),
            Ty::Str => "str".to_string(),
            Ty::Struct(id, _) => format!("S{id}"),
            Ty::Enum(id, _) => format!("E{id}"),
            Ty::Tuple(items) => {
                let items: Vec<String> = items.iter().map(Ty::name).collect();
                format!("({})", items.join(", "))
            }
            Ty::List(of) => format!("List[{}]", of.name()),
        }
    }

    /// The declarations of every struct and enum the type mentions.
    fn declarations(&self, out: &mut Vec<(usize, String)>) {
        match self {
            Ty::Struct(id, fields) => {
                for f in fields {
                    f.declarations(out);
                }
                let body: Vec<String> = (fields.iter().enumerate())
                    .map(|(k, f)| format!("  f{k}: {}\n", f.name()))
                    .collect();
                out.push((*id, format!("struct S{id} {{\n{}}}\n", body.concat())));
            }
            Ty::Enum(id, cases) => {
                for c in cases.iter().flatten() {
                    c.declarations(out);
                }
                let body: Vec<String> = (cases.iter().enumerate())
                    .map(|(k, c)| match c {
                        Some(t) => format!("  c{k}: {}\n", t.name()),
                        None => format!("  c{k}\n"),
                    })
                    .collect();
                out.push((*id, format!("enum E{id} {{\n{}}}\n", body.concat())));
            }
            Ty::Tuple(items) => items.iter().for_each(|t| t.declarations(out)),
            Ty::List(of) => of.declarations(out),
            _ => {}
        }
    }

    /// The source of the struct and enum types, each once.
    fn source(types: &[&Ty]) -> String {
        let mut all = Vec::new();
        for t in types {
            t.declarations(&mut all);
        }
        let mut seen = Vec::new();
        let mut text = String::new();
        for (id, declaration) in all {
            if !seen.contains(&id) {
                seen.push(id);
                text += &declaration;
                text += "\n";
            }
        }
        text
    }

    /// The size in bytes of each scalar a value of the type is held as in
    /// memory, in order. Not for an enum.
    fn scalars(&self) -> Vec<usize> {
        match self {
            Ty::Int(t) => vec![t.bits as usize / 8],
            Ty::Bool => vec![1],
            Ty::Str => vec![8, 8],
            Ty::List(_) => vec![8, 8, 8],
            Ty::Struct(_, parts) | Ty::Tuple(parts) => parts.iter().flat_map(Ty::scalars).collect(),
            Ty::Enum(..) => unreachable!("not asked of an enum"),
        }
    }

    /// How many bytes an element of the type takes in a list.
    fn stride(&self) -> usize {
        let scalars = self.scalars();
        let mut end: usize = 0;
        for s in &scalars {
            end = end.next_multiple_of(*s) + s;
        }
        end.next_multiple_of(scalars.iter().copied().max().unwrap_or(1))
    }

    fn value(&self, g: &mut Gen) -> V {
        match self {
            Ty::Int(t) => V::N(value(g, *t)),
            Ty::Bool => V::B(g.one_in(2)),
            Ty::Str => V::S((*g.pick(&["", "ada", "alan", "x y", "grace hopper"])).to_string()),
            Ty::Struct(_, parts) | Ty::Tuple(parts) => {
                V::Parts(parts.iter().map(|t| t.value(g)).collect())
            }
            Ty::Enum(_, cases) => {
                let k = g.below(cases.len());
                V::Case(k, cases[k].as_ref().map(|t| Box::new(t.value(g))))
            }
            Ty::List(of) => V::L((0..g.below(4)).map(|_| of.value(g)).collect()),
        }
    }

    /// The value written as source.
    fn written(&self, v: &V) -> String {
        match (self, v) {
            (_, V::N(n)) => n.to_string(),
            (_, V::B(b)) => b.to_string(),
            (_, V::S(s)) => format!("\"{s}\""),
            (Ty::Struct(id, parts), V::Parts(vs)) => {
                let args: Vec<String> = (parts.iter().zip(vs).enumerate())
                    .map(|(k, (t, v))| format!("f{k}: {}", t.written(v)))
                    .collect();
                format!("S{id}({})", args.join(", "))
            }
            (Ty::Tuple(parts), V::Parts(vs)) => {
                let items: Vec<String> = parts.iter().zip(vs).map(|(t, v)| t.written(v)).collect();
                format!("({})", items.join(", "))
            }
            (Ty::Enum(id, cases), V::Case(k, payload)) => match (&cases[*k], payload) {
                (Some(t), Some(p)) => format!("E{id}.c{k}({})", t.written(p)),
                _ => format!("E{id}.c{k}"),
            },
            (Ty::List(of), V::L(items)) => {
                let items: Vec<String> = items.iter().map(|v| of.written(v)).collect();
                format!("[{}]", items.join(", "))
            }
            _ => unreachable!("a value of its type"),
        }
    }

    /// What `dump` prints for the value, a line for each part.
    fn lines(&self, v: &V) -> Vec<String> {
        match (self, v) {
            (_, V::N(n)) => vec![n.to_string()],
            (_, V::B(b)) => vec![b.to_string()],
            (_, V::S(s)) => vec![s.clone()],
            (Ty::Struct(_, parts) | Ty::Tuple(parts), V::Parts(vs)) => {
                parts.iter().zip(vs).flat_map(|(t, v)| t.lines(v)).collect()
            }
            (Ty::Enum(_, cases), V::Case(k, payload)) => {
                let mut lines = vec![format!("c{k}")];
                if let (Some(t), Some(p)) = (&cases[*k], payload) {
                    lines.extend(t.lines(p));
                }
                lines
            }
            (Ty::List(of), V::L(items)) => {
                let mut lines = vec![items.len().to_string()];
                lines.extend(items.iter().flat_map(|v| of.lines(v)));
                lines
            }
            _ => unreachable!("a value of its type"),
        }
    }

    /// Statements that print the value `e`, a name or a field or an element
    /// of one, a line for each part, in a function that may print. `n` makes
    /// the names they bind.
    fn dump(&self, e: &str, n: &mut usize) -> String {
        match self {
            Ty::Int(_) => format!("print(\"{{{e}}}\")\n"),
            Ty::Bool => format!("if {e} {{ print(\"true\") }} else {{ print(\"false\") }}\n"),
            Ty::Str => format!("print({e})\n"),
            Ty::Struct(_, parts) => (parts.iter().enumerate())
                .map(|(k, t)| t.dump(&format!("{e}.f{k}"), n))
                .collect(),
            Ty::Tuple(parts) => {
                *n += 1;
                let names: Vec<String> = (0..parts.len()).map(|k| format!("t{n}_{k}")).collect();
                let body: String = (parts.iter().zip(&names))
                    .map(|(t, name)| t.dump(name, n))
                    .collect();
                format!(
                    "match {e} {{\n  ({}) {{\n{body}  }}\n}}\n",
                    names.join(", ")
                )
            }
            Ty::Enum(_, cases) => {
                *n += 1;
                let id = *n;
                let arms: String = (cases.iter().enumerate())
                    .map(|(k, c)| match c {
                        Some(t) => {
                            let body = t.dump(&format!("p{id}"), n);
                            format!(".c{k}(p{id}) {{\nprint(\"c{k}\")\n{body}}}\n")
                        }
                        None => format!(".c{k} {{ print(\"c{k}\") }}\n"),
                    })
                    .collect();
                format!("match {e} {{\n{arms}}}\n")
            }
            Ty::List(of) => {
                *n += 1;
                let id = *n;
                let body = of.dump(&format!("d{id}[i{id}]"), n);
                format!(
                    "d{id} = {e}\nprint(\"{{d{id}.size}}\")\nloop i{id} in 0..d{id}.size {{\n{body}}}\n"
                )
            }
        }
    }
}

/// A program that builds a list of a type from a literal and a run of
/// pushes, passes it on one way, and prints it.
#[derive(Debug)]
struct Held {
    ty: Ty,
    start: Vec<V>,
    pushes: Vec<V>,
    var: bool,
    /// How the list is passed on before it is printed: not at all, through
    /// a function, a struct, a tuple, or an enum.
    through: usize,
}

fn held(g: &mut Gen) -> Held {
    let ty = ty(g, 3, EVERY, &mut 0);
    let start = (0..g.below(3)).map(|_| ty.value(g)).collect();
    let pushes = (0..g.below(6)).map(|_| ty.value(g)).collect();
    Held {
        ty,
        start,
        pushes,
        var: g.one_in(2),
        through: g.below(5),
    }
}

fn items(ty: &Ty, vs: &[V]) -> String {
    let items: Vec<String> = vs.iter().map(|v| ty.written(v)).collect();
    format!("[{}]", items.join(", "))
}

/// For every type built from whole numbers, `bool`, text, structs, enums,
/// tuples, and lists, to the depth the generator reaches, a list of the type
/// built from a literal and a run of pushes, to a `var` or one name after
/// another, and passed through a function, a struct, a tuple, or an enum,
/// holds what was put in, in order.
fn a_list_of_any_type_holds_what_was_put_in_in_order() {
    generate::check(83, 150, held, |h| {
        let t = h.ty.name();
        let mut body = String::new();
        let last = if h.var {
            body += &format!("var xs: List[{t}] = {}\n", items(&h.ty, &h.start));
            for v in &h.pushes {
                body += &format!("xs = push(xs, {})\n", h.ty.written(v));
            }
            "xs".to_string()
        } else {
            body += &format!("l0: List[{t}] = {}\n", items(&h.ty, &h.start));
            for (k, v) in h.pushes.iter().enumerate() {
                body += &format!("l{} = push(l{k}, {})\n", k + 1, h.ty.written(v));
            }
            format!("l{}", h.pushes.len())
        };
        let pass = match h.through {
            0 => format!("ys = {last}\n"),
            1 => format!("ys = through({last})\n"),
            2 => format!("h = Held(items: {last})\nys = h.items\n"),
            3 => format!("ys = untuple(({last}, 1))\n"),
            _ => format!("ys = unbox(Boxed.full({last}))\n"),
        };
        body += &pass;
        let list = Ty::List(Box::new(h.ty.clone()));
        body += &list.dump("ys", &mut 0);
        let source = format!(
            "{}struct Held {{\n  items: List[{t}]\n}}\n\nenum Boxed {{\n  empty\n  full: List[{t}]\n}}\n\nfn through(xs: List[{t}]) -> List[{t}] {{ xs }}\n\nfn untuple(t: (List[{t}], u8)) -> List[{t}] {{\n  match t {{\n    (xs, k) {{ xs }}\n  }}\n}}\n\nfn unbox(b: Boxed) -> List[{t}] {{\n  match b {{\n    .empty {{ [] }}\n    .full(xs) {{ xs }}\n  }}\n}}\n\nfn main() ! console, alloc {{\n{body}}}\n",
            Ty::source(&[&h.ty]),
        );
        let all: Vec<V> = h.start.iter().chain(&h.pushes).cloned().collect();
        prints(&source, list.lines(&V::L(all)));
    });
}

/// The holders of a list of bytes: each is built from a list and gives it
/// back.
const HOLDERS: &[(&str, &str, &str)] = &[
    ("Wrap(xs: {})", "{}.xs", "Wrap"),
    ("({}, 7)", "first({})", "(List[u8], u8)"),
    (".some({})", "inner({})", "Option[List[u8]]"),
    ("[{}]", "head({})", "List[List[u8]]"),
];

const HOLDER_ITEMS: &str = "struct Wrap {\n  xs: List[u8]\n}\n\nfn first(t: (List[u8], u8)) -> List[u8] {\n  match t {\n    (xs, k) { xs }\n  }\n}\n\nfn inner(o: Option[List[u8]]) -> List[u8] {\n  match o {\n    .some(xs) { xs }\n    .none { [] }\n  }\n}\n\nfn head(ls: List[List[u8]]) -> List[u8] {\n  if 0 < ls.size { ls[0] } else { [] }\n}\n\nfn grow(xs: List[u8]) -> List[u8] ! alloc {\n  var grown = xs\n  grown = push(grown, 9)\n  grown\n}\n\n";

fn fill(pattern: &str, with: &str) -> String {
    pattern.replace("{}", with)
}

/// A program that holds lists of bytes in structs, tuples, enums, and lists
/// and pushes to others, whose model says what each holds at the end.
#[derive(Debug)]
struct Shared {
    source: String,
    lines: Vec<String>,
}

fn shared(g: &mut Gen) -> Shared {
    let mut vars: Vec<Vec<u8>> = Vec::new();
    let mut holders: Vec<(usize, Vec<u8>)> = Vec::new();
    let mut body = String::new();
    let byte = |g: &mut Gen| g.below(256) as u8;
    for _ in 0..1 + g.below(14) {
        let step = if vars.is_empty() { 0 } else { g.below(12) };
        let j = if vars.is_empty() {
            0
        } else {
            g.below(vars.len())
        };
        let h = if holders.is_empty() {
            0
        } else {
            g.below(holders.len())
        };
        match step {
            0 => {
                let new: Vec<u8> = (0..g.below(4)).map(|_| byte(g)).collect();
                let shown: Vec<String> = new.iter().map(u8::to_string).collect();
                body += &format!("var a{}: List[u8] = [{}]\n", vars.len(), shown.join(", "));
                vars.push(new);
            }
            1..=3 => {
                let v = byte(g);
                body += &format!("a{j} = push(a{j}, {v})\n");
                vars[j].push(v);
            }
            4 | 5 => {
                let kind = g.below(HOLDERS.len());
                body += &format!(
                    "h{}: {} = {}\n",
                    holders.len(),
                    [
                        "Wrap",
                        "(List[u8], u8)",
                        "Option[List[u8]]",
                        "List[List[u8]]"
                    ][kind],
                    fill(HOLDERS[kind].0, &format!("a{j}"))
                );
                holders.push((kind, vars[j].clone()));
            }
            6 | 7 if !holders.is_empty() => {
                let (kind, held) = holders[h].clone();
                body += &format!(
                    "var a{} = {}\n",
                    vars.len(),
                    fill(HOLDERS[kind].1, &format!("h{h}"))
                );
                vars.push(held);
            }
            8 if !holders.is_empty() => {
                let (kind, held) = holders[h].clone();
                body += &format!("a{j} = {}\n", fill(HOLDERS[kind].1, &format!("h{h}")));
                vars[j] = held;
            }
            9 => {
                body += &format!("a{j} = grow(a{j})\n");
                vars[j].push(9);
            }
            10 => {
                let v = byte(g);
                body += &format!("loop i in 0..3 {{ a{j} = push(a{j}, {v}) }}\n");
                vars[j].extend([v; 3]);
            }
            _ => {
                let new = vars[j].clone();
                body += &format!("var a{} = a{j}\n", vars.len());
                vars.push(new);
            }
        }
    }
    let dump = Elem::Int(INTS[1]);
    let mut lines = Vec::new();
    for (k, (kind, held)) in holders.iter().enumerate() {
        body += &format!("g{k} = {}\n", fill(HOLDERS[*kind].1, &format!("h{k}")));
        body += &dump.dump(&format!("g{k}"));
        lines.extend(shown(
            &held
                .iter()
                .map(|b| Val::N(i128::from(*b)))
                .collect::<Vec<_>>(),
        ));
    }
    for (k, items) in vars.iter().enumerate() {
        body += &format!("f{k} = a{k}\n");
        body += &dump.dump(&format!("f{k}"));
        lines.extend(shown(
            &items
                .iter()
                .map(|b| Val::N(i128::from(*b)))
                .collect::<Vec<_>>(),
        ));
    }
    let source = format!("{HOLDER_ITEMS}fn main() ! console, alloc {{\n{body}}}\n");
    Shared { source, lines }
}

/// A list is a value however deep it lies: a name given a value that holds a
/// list still holds the same elements after any other name is pushed to,
/// whether the list lies in a struct, a tuple, an enum, or a list, and
/// whether it was put there or taken out, in every order of binding and
/// pushing.
fn a_list_held_inside_another_value_is_unchanged_by_a_push_to_any_other_name() {
    generate::check(89, 300, shared, |s| prints(&s.source, s.lines.clone()));
}

/// How many times a program asks the system for memory.
fn requests(source: &str) -> usize {
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
    let (outcome, requests) = scalar::run_taking(&program, usize::MAX);
    assert_eq!(outcome.status, 0, "for {source}");
    requests.len()
}

/// A push to a list held inside another value, or a list that a function
/// was given and gives back, asks for memory when the list is full and not
/// otherwise, as a push to a `var` does.
fn a_push_to_a_list_held_inside_another_value_grows_in_place_while_it_has_room() {
    for (kind, (build, take, ty)) in HOLDERS.iter().enumerate() {
        for n in 1usize..=9 {
            let program = |push: &str| {
                format!(
                    "{HOLDER_ITEMS}fn main() ! alloc {{\n  var a: List[u8] = []\n  loop i in 0..{n} {{ a = push(a, 1) }}\n  h: {ty} = {}\n  var b = {}\n{push}}}\n",
                    fill(build, "a"),
                    fill(take, "h"),
                )
            };
            let (without, with) = (
                requests(&program("")),
                requests(&program("  b = push(b, 2)\n")),
            );
            // The list has room for the next power of two.
            let full = n.is_power_of_two();
            assert_eq!(with, without + usize::from(full), "holder {kind}, {n} elements");
        }
    }
}

/// How many times the program asks the system for memory, when it pushes to
/// a list `n` times in the way `loop` says, with `setup` before it.
fn pushes_by(setup: &str, loop_body: &str, n: usize) -> usize {
    requests(&format!(
        "{setup}fn main() ! alloc {{\n  var a = Acc(items: [], n: 0)\n  var items: List[u64] = []\n  loop i in 0..{n} {{ {loop_body} }}\n}}\n"
    ))
}

const ACC: &str = "struct Acc {\n  items: List[u64]\n  n: u64\n}\n\nfn add(a: Acc, x: u64) -> Acc ! alloc {\n  var next = a\n  next.items = push(a.items, x)\n  next\n}\n\nfn more(items: List[u64], x: u64) -> List[u64] ! alloc { push(items, x) }\n\n";

/// A list grown in the way `form` says asks for memory as many times as one
/// grown in a plain `var` may, as many as the logarithm of the pushes, however
/// many pushes there are.
fn grows_as_a_plain_var_does(form: &str) {
    for n in [1000, 10000, 100000] {
        let asked = pushes_by(ACC, form, n);
        assert!(asked <= 1 + ceil_log2(n), "{asked} requests for {form}, {n} pushes");
    }
}

/// A list that a function takes, grows, and returns grows as a plain `var` does.
fn a_list_that_a_function_takes_in_a_struct_and_returns_grows_as_a_plain_var_does() {
    grows_as_a_plain_var_does("a = add(a, i)");
}

/// A list that is a field of a `var` grows as a plain `var` does.
fn a_list_that_is_a_field_of_a_var_grows_as_a_plain_var_does() {
    grows_as_a_plain_var_does("a.items = push(a.items, i)");
}

/// A list that a function pushes to and returns grows as a plain `var` does.
fn a_list_that_a_function_pushes_to_and_returns_grows_as_a_plain_var_does() {
    grows_as_a_plain_var_does("items = more(items, i)");
}

/// An element of a list is held as its scalars one after another, each at a
/// multiple of its size, and takes a whole number of its largest scalar, so
/// that a list of `n` of them has room for between `n` and `2n` elements of
/// the size the types give.
fn an_element_takes_its_scalars_at_their_sizes_and_a_whole_number_of_its_largest() {
    let mixed = "struct M {\n  a: u8\n  b: u32\n  c: u8\n}\n\nfn main() ! alloc {\n  var xs: List[M] = []\n  loop i in 0..3 { xs = push(xs, M(a: 1, b: 2, c: 3)) }\n}\n";
    let program = lower(mixed.as_bytes()).expect("compiles");
    let (_, asked) = scalar::run_taking(&program, usize::MAX);
    // `a` at 0, `b` at 4, `c` at 8, which ends at 9, and 12 is a whole number
    // of 4, so the last list has room for 4 elements of 12 bytes.
    assert_eq!(asked.last(), Some(&(WORD + 48)), "{asked:?}");
    let kinds = Kinds {
        enums: false,
        lists: false,
    };
    generate::check(
        97,
        100,
        |g| {
            let t = ty(g, 3, kinds, &mut 0);
            let v = t.value(g);
            (t, v, 1 + g.below(40))
        },
        |(t, v, n)| {
            let source = format!(
                "{}fn main() ! alloc {{\n  var xs: List[{}] = []\n  loop i in 0..{n} {{ xs = push(xs, {}) }}\n}}\n",
                Ty::source(&[t]),
                t.name(),
                t.written(v),
            );
            let program = lower(source.as_bytes()).expect("compiles");
            let (_, asked) = scalar::run_taking(&program, usize::MAX);
            let room = *asked.last().expect("memory was asked for") - WORD;
            let stride = t.stride();
            assert_eq!(room % stride, 0, "{room} bytes for {source}");
            assert!(room >= n * stride, "{room} bytes for {source}");
            assert!(room <= 2 * n * stride, "{room} bytes for {source}");
        },
    );
}

/// `xs[i].field` reads the scalars of the field and no others: a function
/// that reads it has as many loads as the field has scalars.
fn reading_a_field_of_an_element_reads_only_the_scalars_of_that_field() {
    let kinds = Kinds {
        enums: false,
        lists: true,
    };
    generate::check(
        101,
        100,
        |g| {
            let mut next = 100;
            let fields: Vec<Ty> = (0..2 + g.below(3))
                .map(|_| ty(g, 2, kinds, &mut next))
                .collect();
            let at = g.below(fields.len());
            let value = Ty::Struct(0, fields.clone()).value(g);
            (fields, at, value)
        },
        |(fields, at, value)| {
            let element = Ty::Struct(0, fields.clone());
            let field = &fields[*at];
            let source = format!(
                "{}fn get(xs: List[S0], i: Nat) -> {} requires i < xs.size {{ xs[i].f{at} }}\n\nfn main() ! alloc {{\n  xs: List[S0] = [{}]\n  x = get(xs, 0)\n}}\n",
                Ty::source(&[&element]),
                field.name(),
                element.written(value),
            );
            let program = lower(source.as_bytes()).expect("compiles");
            let loads: usize = (program.functions.iter().enumerate())
                .filter(|(k, f)| *k != program.main && !handler::takes_room(&program, f))
                .flat_map(|(_, f)| &f.blocks)
                .flat_map(|b| &b.instructions)
                .filter(|i| matches!(i.op, Op::Load { .. }))
                .count();
            assert_eq!(loads, field.scalars().len(), "for {source}");
        },
    );
}

/// The program of the issue: a loop over a list sums it, and needs neither
/// `diverge` nor anything but the effects of its body.
fn a_loop_over_a_list_sums_it() {
    let source = "fn main() ! console, alloc {\n  xs: List[u64] = [1, 2, 3]\n  var sum: u64 = 0\n  loop v in xs { sum = wrapping_add(sum, v) }\n  print(\"{sum}\")\n}\n";
    prints(source, vec!["6".into()]);
}

/// A loop over a list of a type, a `break` at some round, and a push to the
/// list in each round.
#[derive(Debug)]
struct Looped {
    ty: Ty,
    items: Vec<V>,
    /// What is pushed to the list before the loop, which may leave it room.
    before: Vec<V>,
    /// The round that breaks, when one does.
    stop: Option<usize>,
    /// What each round pushes to the list it loops over, when it does.
    push: Option<V>,
}

fn looped(g: &mut Gen) -> Looped {
    let ty = ty(g, 3, EVERY, &mut 0);
    let items = (0..g.below(4)).map(|_| ty.value(g)).collect();
    let before = (0..g.below(4)).map(|_| ty.value(g)).collect();
    let stop = g.one_in(2).then(|| g.below(6));
    let push = g.one_in(2).then(|| ty.value(g));
    Looped {
        ty,
        items,
        before,
        stop,
        push,
    }
}

/// For every list of every type the generator builds, `loop v in xs` gives
/// `v` each element from the first to the last, once each, and not at all
/// for an empty list, whatever the block does to the `var` list `xs`, and a
/// `break` ends it.
fn a_loop_over_a_list_of_any_type_gives_each_element_of_the_list_it_began_with_until_a_break() {
    generate::check(97, 30, looped, |l| {
        let t = l.ty.name();
        let mut setup = String::new();
        for v in &l.before {
            setup += &format!("ys = push(ys, {})\n", l.ty.written(v));
        }
        let mut block = String::new();
        if let Some(k) = l.stop {
            block += &format!("if seen == {k} {{ break }}\n");
        }
        block += "seen = wrapping_add(seen, 1)\n";
        if let Some(extra) = &l.push {
            block += &format!("ys = push(ys, {})\n", l.ty.written(extra));
        }
        block += &l.ty.dump("v", &mut 0);
        let source = format!(
            "{}fn main() ! console, alloc {{\nvar ys: List[{t}] = {}\nvar seen: u64 = 0\n{setup}loop v in ys {{\n{block}}}\nprint(\"{{ys.size}}\")\n}}\n",
            Ty::source(&[&l.ty]),
            items(&l.ty, &l.items)
        );
        let all: Vec<&V> = l.items.iter().chain(&l.before).collect();
        let rounds = all.len().min(l.stop.unwrap_or(usize::MAX));
        let mut lines: Vec<String> = (all[..rounds].iter())
            .flat_map(|v| l.ty.lines(v))
            .collect();
        let pushed = if l.push.is_some() { rounds } else { 0 };
        lines.push((all.len() + pushed).to_string());
        prints(&source, lines);
    });
}

/// For every type of element, the function that sums a list with `loop v in
/// xs` has what the one that sums it with `xs[i]` in a loop over `0..xs.size`
/// has: one comparison, one load, and the same calls.
fn a_loop_over_a_list_is_one_comparison_and_one_load_of_the_element_each_time_round() {
    let counted = |source: &str| {
        let program = lower(source.as_bytes()).expect("compiles");
        let (mut compares, mut loads, mut calls) = (0, 0, 0);
        for f in program
            .functions
            .iter()
            .enumerate()
            .filter(|(at, f)| *at != program.main && !handler::takes_room(&program, f))
            .map(|(_, f)| f)
        {
            for block in &f.blocks {
                for instruction in &block.instructions {
                    match &instruction.op {
                        Op::Compare { .. } => compares += 1,
                        Op::Load { .. } => loads += 1,
                        Op::Call { .. } => calls += 1,
                        _ => {}
                    }
                }
            }
        }
        (compares, loads, calls)
    };
    for ty in INTS {
        let t = ty.name;
        let sum = |head: &str, element: &str| {
            format!(
                "fn total(xs: List[{t}]) -> {t} {{\n  var sum: {t} = 0\n  {head} {{ sum = wrapping_add(sum, {element}) }}\n  sum\n}}\n\nfn main() ! alloc {{\n  xs: List[{t}] = [1, 2, 3]\n  total(xs)\n}}\n"
            )
        };
        let each = sum("loop v in xs", "v");
        let (compares, loads, calls) = counted(&each);
        assert_eq!((compares, loads), (1, 1), "for {each}");
        let indexed = sum("loop i in 0..xs.size", "xs[i]");
        assert_eq!(calls, counted(&indexed).2, "for {each}");
    }
}

/// A loop runs over the numbers of a range or the elements of a list, and
/// what else it is given is refused, naming its type.
fn a_loop_runs_over_a_range_or_a_list_and_nothing_else() {
    for ty in INTS {
        let t = ty.name;
        let source = format!(
            "fn main() ! console, alloc {{\n  n: {t} = 3\n  loop v in n {{ print(\"{{v}}\") }}\n}}\n"
        );
        let errors = refused(&source);
        assert!(
            errors.iter().any(|e| e.contains(&format!("`{t}`"))),
            "{errors:?} does not name `{t}` for {source}"
        );
    }
}
