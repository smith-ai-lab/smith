//! `Hash`, which every type that can be a key is given: that equal values
//! have one hash, that the hashes of different values spread, and what
//! `without` and an `impl` do to it.

use smith::scalar;
use smith::{check, lower};

use crate::Test;
use crate::generate::{self, Gen};

pub const ONCE: &[Test] = &[
    (
        "values_that_are_equal_have_the_same_hash",
        values_that_are_equal_have_the_same_hash,
    ),
    (
        "the_hashes_of_numbers_spread_over_the_low_16_bits",
        the_hashes_of_numbers_spread_over_the_low_16_bits,
    ),
    (
        "the_hashes_of_text_and_structs_spread_over_the_low_16_bits",
        the_hashes_of_text_and_structs_spread_over_the_low_16_bits,
    ),
    (
        "the_hashes_of_enums_and_lists_spread_over_the_low_16_bits",
        the_hashes_of_enums_and_lists_spread_over_the_low_16_bits,
    ),
    (
        "a_type_has_no_hash_when_without_takes_it_or_a_part_lacks_it",
        a_type_has_no_hash_when_without_takes_it_or_a_part_lacks_it,
    ),
    (
        "an_impl_of_hash_replaces_the_one_a_type_was_given",
        an_impl_of_hash_replaces_the_one_a_type_was_given,
    ),
];

/// The types a key may have, each as the type, and an expression that makes
/// a value of it from the number `n` and a number `k` that differs for every
/// `n`, both `u64`. Two values made from the same `n` and `k` are equal. The declarations that the expressions use come first.
const DECLARATIONS: &str = "\
struct Pair {
  a: u64
  b: str
}

enum Shape {
  dot
  line: u32
  name: str
}

fn shape(n: u64, k: u64) -> Shape ! alloc {
  if bit_and(n, 1) == 0 { Shape.name(\"{k}\") } else { Shape.line(wrapping_narrow(k)) }
}

fn numbers(n: u64, k: u64) -> List[u64] ! alloc {
  xs: List[u64] = []
  push(push(xs, n), k)
}

fn words(n: u64, k: u64) -> List[str] ! alloc {
  xs: List[str] = []
  push(push(xs, \"{n}\"), \"{k}\")
}
";

/// The `use` lines of a program that has `DECLARATIONS`, and a `main` that makes
/// values of these types: the sizes it names are imported, and no other.
fn header(values: &[(&str, &str)]) -> String {
    let mut bits = vec!["bit_and"];
    for size in ["i32", "u16", "u32", "u64"] {
        if size == "u32" || size == "u64" || values.iter().any(|(ty, _)| ty == &size) {
            bits.push(size);
        }
    }
    format!(
        "use std.bits ({})\nuse std.console (console, print)\nuse std.int (wrapping_add, wrapping_mul, wrapping_narrow)\nuse std.list (push)\n\n",
        bits.join(", ")
    )
}

const VALUES: &[(&str, &str)] = &[
    ("u64", "k"),
    ("u16", "wrapping_narrow(k)"),
    ("u32", "wrapping_narrow(k)"),
    ("i32", "wrapping_narrow(k)"),
    ("str", "\"{k}\""),
    ("Pair", "Pair(a: k, b: \"{n}\")"),
    ("(u64, u64)", "(n, k)"),
    ("((Byte, u64), str)", "((wrapping_narrow(n), k), \"{n}\")"),
    ("Shape", "shape(n, k)"),
    ("List[u64]", "numbers(n, k)"),
    ("List[str]", "words(n, k)"),
];

/// What the program prints for the hash of each of `count` values of each
/// type, one line each, where the values are `k = n * stride + offset`.
fn spread(values: &[(&str, &str)], count: u64, stride: u64, offset: u64) -> Vec<Vec<u64>> {
    let mut source = header(values);
    source.push_str(DECLARATIONS);
    source.push_str("\nfn main() ! console, alloc {\n");
    for (ty, make) in values {
        source.push_str(&format!(
            "  loop i in 0..{count} {{\n    n: u64 = i\n    k: u64 = wrapping_add(wrapping_mul(n, {stride}), {offset})\n    x: {ty} = {make}\n    print(\"{{hash(x)}}\")\n  }}\n"
        ));
    }
    source.push_str("}\n");
    let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} in\n{source}"));
    let program = scalar::passes(&program);
    let out = scalar::run(&program).stdout;
    let text = String::from_utf8(out).unwrap();
    let all: Vec<u64> = text.lines().map(|l| l.parse().unwrap()).collect();
    assert_eq!(all.len() as u64, count * values.len() as u64);
    all.chunks(count as usize).map(<[u64]>::to_vec).collect()
}

fn a_stride_and_an_offset(g: &mut Gen) -> (u64, u64) {
    let stride = 2 * g.below(1 << 20) as u64 + 1;
    let offset = g.below(1 << 30) as u64 * 4099;
    (stride, offset)
}

/// Checks that the hashes of 1000 different values of each type spread. The
/// types are in several tests, so that each takes a fraction of a second.
fn spread_of(values: &[(&str, &str)], seed: u64) {
    const COUNT: u64 = 1000;
    // Hashes that fell at random would leave about COUNT² / (2 · 65536)
    // values sharing the low 16 bits of another. Four times that, and a few
    // more, is far beyond what chance gives and far below what a hash that
    // keeps its input's low bits gives.
    let bound = 4 * COUNT * COUNT / (2 * 65536) + 4;
    generate::check(seed, 1, a_stride_and_an_offset, |&(stride, offset)| {
        for (hashes, (ty, _)) in spread(values, COUNT, stride, offset).iter().zip(values) {
            let mut low: Vec<u64> = hashes.iter().map(|h| h & 0xffff).collect();
            low.sort_unstable();
            low.dedup();
            let shared = COUNT - low.len() as u64;
            assert!(shared <= bound, "{shared} of {COUNT} {ty} share low bits");
        }
    });
}

fn the_hashes_of_numbers_spread_over_the_low_16_bits() {
    spread_of(&VALUES[..4], 261);
}

fn the_hashes_of_text_and_structs_spread_over_the_low_16_bits() {
    spread_of(&VALUES[4..8], 264);
}

fn the_hashes_of_enums_and_lists_spread_over_the_low_16_bits() {
    spread_of(&VALUES[8..], 265);
}

fn values_that_are_equal_have_the_same_hash() {
    generate::check(262, 3, a_stride_and_an_offset, |&(stride, offset)| {
        let mut source = header(VALUES);
        source.push_str(DECLARATIONS);
        source.push_str("\nfn main() ! console, alloc {\n");
        for (ty, make) in VALUES {
            source.push_str(&format!(
                "  loop i in 0..20 {{\n    n: u64 = i\n    k: u64 = wrapping_add(wrapping_mul(n, {stride}), {offset})\n    x: {ty} = {make}\n    y: {ty} = {make}\n    if hash(x) != hash(y) {{ print(\"{ty}\") }}\n    print(\"same\")\n  }}\n"
            ));
        }
        source.push_str("}\n");
        let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} in\n{source}"));
        let out = String::from_utf8(scalar::run(&program).stdout).unwrap();
        assert!(out.lines().all(|l| l == "same"), "{out}");
        assert_eq!(out.lines().count(), 20 * VALUES.len());
    });
}

/// The messages of the errors a source gives.
fn errors(source: &str) -> Vec<String> {
    match check(source.as_bytes()) {
        Ok(_) => Vec::new(),
        Err(found) => found.into_iter().map(|e| e.message).collect(),
    }
}

fn a_type_has_no_hash_when_without_takes_it_or_a_part_lacks_it() {
    let key = "use std.console (console, print)\n\nstruct Key {\n  secret: str\n}\n\nwithout Hash for Key\n\nstruct Door {\n  key: Key\n}\n";
    let main = |x: &str| format!("{key}\nfn main() ! console, alloc {{\n  print(\"{{hash({x})}}\")\n}}\n");
    let key_alone = errors(&main("Key(secret: \"a\")"));
    assert_eq!(key_alone.len(), 1, "{key_alone:?}");
    assert!(key_alone[0].contains("Hash") && key_alone[0].contains("Key"), "{key_alone:?}");
    let door = errors(&main("Door(key: Key(secret: \"a\"))"));
    assert_eq!(door.len(), 1, "{door:?}");
    assert!(door[0].contains("Hash") && door[0].contains("Door") && door[0].contains("Key"), "{door:?}");
    let in_list = errors(&format!(
        "{key}\nfn main() ! console, alloc {{\n  xs: List[Key] = []\n  print(\"{{hash(xs)}}\")\n}}\n"
    ));
    assert_eq!(in_list.len(), 1, "{in_list:?}");
    assert!(in_list[0].contains("Hash") && in_list[0].contains("Key"), "{in_list:?}");
    // Taking `Hash` from a type leaves it its `Eq`.
    let eq = errors(&format!(
        "{key}\nfn main() ! console {{\n  a = Key(secret: \"a\")\n  if a == a {{ print(\"same\") }}\n}}\n"
    ));
    assert!(eq.is_empty(), "{eq:?}");
}

fn an_impl_of_hash_replaces_the_one_a_type_was_given() {
    generate::check(263, 8, |g| g.below(1000) as u64, |&n| {
        let source = format!(
            "use std.bits (u64)\nuse std.console (console, print)\n\nstruct Cat {{\n  name: str\n}}\n\nstruct Home {{\n  cat: Cat\n}}\n\nimpl Hash[Cat] {{\n  fn hash(value: Cat) -> u64 {{ {n} }}\n}}\n\nfn main() ! console, alloc {{\n  print(\"{{hash(Cat(name: \"tom\"))}}\")\n  print(\"{{hash(Home(cat: Cat(name: \"tim\")))}}\")\n}}\n"
        );
        let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} in\n{source}"));
        let out = String::from_utf8(scalar::run(&program).stdout).unwrap();
        let lines: Vec<&str> = out.lines().collect();
        assert_eq!(lines[0], n.to_string());
        // A struct is hashed from its parts, so a part's own `impl` counts.
        let home: u64 = lines[1].parse().unwrap();
        let other = source.replace(&format!("-> u64 {{ {n} }}"), &format!("-> u64 {{ {} }}", n + 1));
        let program = lower(other.as_bytes()).unwrap();
        let out = String::from_utf8(scalar::run(&program).stdout).unwrap();
        assert_ne!(out.lines().nth(1).unwrap().parse::<u64>().unwrap(), home);
    });
}
