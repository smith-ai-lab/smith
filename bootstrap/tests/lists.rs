//! Generators and properties of `List`: built from a literal and `push`,
//! read by an index that `arith` proves, and a value like any other.
//!
//! Every generator keeps a model of the lists it builds, in plain vectors, so
//! it shares nothing with the compiler.

use smith::scalar::{self, Op};
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};
use crate::numbers::{INTS, Int, value};

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
        "a_list_of_what_is_not_whole_numbers_or_bool_is_refused",
        a_list_of_what_is_not_whole_numbers_or_bool_is_refused,
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
                    "  xs: List[u8] = [{items}]\n  k: u64 = {}\n  if k < xs.size {{ print(\"{{xs[k]}}\") }}\n",
                    r.j
                ),
                true,
                "k < xs.size".to_string(),
            ),
            3 => (
                format!(
                    "  xs: List[u8] = [{items}]\n  k: u64 = {}\n  print(\"{{xs[k]}}\")\n",
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
            "fn at(xs: List[u8], k: u64) -> u8\n  requires k < xs.size\n{{\n  xs[k]\n}}\n\nfn main() ! console, alloc {{\n{body}}}\n"
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
            .filter(|(at, _)| *at != program.main)
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
        let (outcome, requests) = scalar::run_counting(&program, usize::MAX);
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
        let (_, requests) = scalar::run_counting(&program, usize::MAX);
        let room = *requests.last().expect("memory was asked for");
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

fn a_list_of_what_is_not_whole_numbers_or_bool_is_refused() {
    let source = "fn main() ! alloc {\n  xs: List[str] = []\n}\n";
    let errors = refused(source);
    assert!(
        errors.iter().any(|e| e.contains("not supported yet")),
        "{errors:?} for {source}"
    );
}
