//! What a push to a list in a loop costs, read from the executables the
//! compiler writes: the instructions of a round of the loop, and that the
//! growth of the list is as it was.

use smith::scalar;
use smith::target::Target;
use smith::{compile, lower};

use crate::Test;
use crate::aarch64;
use crate::generate::{self, Gen};
use crate::growth;
use crate::machine_code::{self, Machine};

pub const ONCE: &[Test] = &[
    (
        "a_push_in_a_loop_takes_at_most_twice_the_instructions_of_rust_on_x86_64",
        a_push_in_a_loop_takes_at_most_twice_the_instructions_of_rust_on_x86_64,
    ),
    (
        "a_push_in_a_loop_takes_at_most_twice_the_instructions_of_rust_on_arm64",
        a_push_in_a_loop_takes_at_most_twice_the_instructions_of_rust_on_arm64,
    ),
    (
        "a_push_loop_asks_for_memory_once_for_each_doubling_of_the_list",
        a_push_loop_asks_for_memory_once_for_each_doubling_of_the_list,
    ),
    (
        "what_an_arm64_executable_pushes_is_what_the_evaluator_says_at_every_width_part_1_of_3",
        || what_an_arm64_executable_pushes_is_what_the_evaluator_says_at_every_width(0),
    ),
    (
        "what_an_arm64_executable_pushes_is_what_the_evaluator_says_at_every_width_part_2_of_3",
        || what_an_arm64_executable_pushes_is_what_the_evaluator_says_at_every_width(1),
    ),
    (
        "what_an_arm64_executable_pushes_is_what_the_evaluator_says_at_every_width_part_3_of_3",
        || what_an_arm64_executable_pushes_is_what_the_evaluator_says_at_every_width(2),
    ),
];

/// The instructions of a round of `for i in 0..n { xs.push(i) }` in Rust built
/// with `-O`, for a `Vec<u64>`: a store, an add, a store of the length, a
/// compare and a branch for the loop, and a compare and a branch for the room.
/// It is what `objdump -d` shows of that loop, and `valgrind --tool=callgrind`
/// counts the same for each push.
const RUST: usize = 7;

/// What the round of the same loop takes in Smith on x86-64: the compare and the
/// branch of the loop, the compare and the branch of the room, the store at the
/// end of the list, the two additions, and the jump back to the loop's test.
const SMITH_X86_64: usize = 8;

/// What it takes on arm64, which is one more: a constant is made in each block
/// that reads it, so the loop's bound is made in the round, by one instruction
/// when it is below 2^16 and by up to four when it is not.
const SMITH_ARM64: usize = 9;

/// A program that pushes the numbers below `pushes` to a list of `u64`.
fn program_of(pushes: i64) -> String {
    format!(
        "use std.bits (u64)\nuse std.console (console, print)\nuse std.list (push)\n\nfn main() ! console, alloc {{\n  var xs: List[u64] = []\n  loop i in 0..{pushes} {{ xs = push(xs, i) }}\n  print(\"{{xs.size}}\")\n}}\n"
    )
}

/// The shortest round of the loop that pushes in the executable for each
/// target that is an ELF file for x86-64, with the number of those.
fn shortest_rounds() -> Vec<usize> {
    // A bound that no other number in the program is, so that the instruction
    // that holds it is the loop's own test.
    const PUSHES: i64 = 1_000_003;
    let source = program_of(PUSHES);
    let mut rounds = Vec::new();
    for target in Target::ALL {
        let bytes = compile(source.as_bytes(), *target).unwrap();
        let Some((Machine::X86_64, found)) = machine_code::instructions(&bytes) else {
            continue;
        };
        rounds.push(machine_code::shortest_round(&found, PUSHES));
    }
    rounds
}

/// The loop that pushes takes 8 instructions a round on x86-64, which is at most
/// twice what Rust's takes.
fn a_push_in_a_loop_takes_at_most_twice_the_instructions_of_rust_on_x86_64() {
    let rounds = shortest_rounds();
    assert!(!rounds.is_empty(), "no target is an ELF file for x86-64");
    for round in rounds {
        assert!(round <= 2 * RUST, "a round takes {round} instructions");
        assert!(round <= SMITH_X86_64, "a round takes {round} instructions, not {SMITH_X86_64}");
    }
}

/// How many instructions the arm64 machine of the tests runs for one more push
/// of the loop, by running it for two numbers of pushes that need the same
/// memory: the difference is the rounds between, and nothing else.
fn rounds_on_arm64() -> Vec<usize> {
    // The list has room for 64 after its 33rd element and for 128 after its
    // 65th, so each of these grows the same times.
    const FEW: i64 = 40;
    const MORE: i64 = 60;
    let mut rounds = Vec::new();
    for target in Target::ALL {
        let steps = |pushes: i64| {
            let bytes = compile(program_of(pushes).as_bytes(), *target).unwrap();
            aarch64::is_executable(&bytes).then(|| {
                let ran = aarch64::run(&bytes);
                assert_eq!(ran.stdout, format!("{pushes}\n").into_bytes());
                ran.steps
            })
        };
        let (Some(few), Some(more)) = (steps(FEW), steps(MORE)) else {
            continue;
        };
        let between = (MORE - FEW) as u64;
        assert_eq!((more - few) % between, 0, "{} is not a number of rounds", more - few);
        rounds.push(((more - few) / between) as usize);
    }
    rounds
}

/// The loop that pushes takes 9 instructions a round on arm64, which is at most
/// twice what Rust's takes on x86-64.
fn a_push_in_a_loop_takes_at_most_twice_the_instructions_of_rust_on_arm64() {
    let rounds = rounds_on_arm64();
    assert!(!rounds.is_empty(), "no target is built for arm64");
    for round in rounds {
        assert!(round <= 2 * RUST, "a round takes {round} instructions");
        assert!(round <= SMITH_ARM64, "a round takes {round} instructions, not {SMITH_ARM64}");
    }
}

/// A list that is pushed to in a loop asks for memory when it has none, and
/// then each time it is full, for twice as many elements as it has, so that
/// pushing `n` elements takes one request and one for each time the list
/// doubles, and none for a push that has room.
fn a_push_loop_asks_for_memory_once_for_each_doubling_of_the_list() {
    let sizes = |g: &mut Gen| 1 + g.below(300) as i64;
    generate::check(5111, 25, sizes, |n| {
        let program = lower(program_of(*n).as_bytes()).unwrap();
        let (ran, taken) = scalar::run_taking(&program, usize::MAX);
        let taken = growth::rooms(&taken);
        assert_eq!(ran.stdout, format!("{n}\n").into_bytes());
        // The requests of the list come first, and then the one of the text
        // the program prints.
        let doublings = (*n as usize).next_power_of_two().trailing_zeros() as usize;
        let asked: Vec<usize> = (0..=doublings).map(|k| 8usize << k).collect();
        assert_eq!(taken[..asked.len()], asked, "for {n} pushes");
        assert!(taken[asked.len()..].iter().all(|bytes| *bytes < 8), "for {n} pushes: {taken:?}");
    });
}

/// A program that pushes to a list of the type, in a loop that also keeps `live`
/// other numbers, so that some of them do not fit in registers, the number
/// `value` to give each time as a literal or as a number that changes, and
/// then prints the size and the sum of the elements, which it reads back.
#[derive(Debug)]
struct Pushes {
    elem: &'static str,
    pushes: u64,
    live: usize,
    /// What is pushed: `None` is a number that changes in each round.
    value: Option<u8>,
    /// What each round adds to the number that changes.
    step: u8,
}

fn pushes(g: &mut Gen) -> Pushes {
    Pushes {
        elem: g.pick(&["u8", "u16", "u32", "u64"]),
        pushes: 1 + g.below(90) as u64,
        live: *g.pick(&[0, 3, 12, 30]),
        value: g.one_in(3).then(|| g.below(256) as u8),
        step: 1 + g.below(200) as u8,
    }
}

/// The program, and what it prints.
fn pushed(p: &Pushes) -> (String, String) {
    let mut live = String::new();
    let mut changes = String::new();
    for k in 0..p.live {
        live.push_str(&format!("  var l{k}: u64 = {}\n", 3 * k + 1));
        changes.push_str(&format!("    l{k} = wrapping_add(l{k}, {})\n", k + 2));
    }
    let item = match p.value {
        Some(v) => v.to_string(),
        None => "v".to_string(),
    };
    let mut sum_live = String::from("0");
    for k in 0..p.live {
        sum_live = format!("wrapping_add({sum_live}, l{k})");
    }
    // `u8` is written `Byte`, and every other size is imported.
    let ty = if p.elem == "u8" { "Byte" } else { p.elem };
    let mut sizes: Vec<&str> = Vec::new();
    if p.elem != "u8" {
        sizes.push(p.elem);
    }
    if p.live > 0 && !sizes.contains(&"u64") {
        sizes.push("u64");
    }
    sizes.sort_unstable();
    let bits = if sizes.is_empty() {
        String::new()
    } else {
        format!("use std.bits ({})\n", sizes.join(", "))
    };
    let source = format!(
        "{bits}use std.console (console, print)\nuse std.int (wrapping_add)\nuse std.list (push)\n\nfn main() ! console, alloc {{\n  var xs: List[{e}] = []\n  var v: {e} = 0\n{live}  loop i in 0..{n} {{\n{changes}    xs = push(xs, {item})\n    v = wrapping_add(v, {step})\n  }}\n  var total: {e} = 0\n  loop x in xs {{ total = wrapping_add(total, x) }}\n  print(\"{{xs.size}} {{total}} {{{sum_live}}}\")\n}}\n",
        e = ty,
        n = p.pushes,
        step = p.step,
    );
    let bits: u32 = match p.elem {
        "u8" => 8,
        "u16" => 16,
        "u32" => 32,
        _ => 64,
    };
    let mask = if bits == 64 { u64::MAX } else { (1u64 << bits) - 1 };
    let mut total: u64 = 0;
    let mut v: u64 = 0;
    for _ in 0..p.pushes {
        let pushed = match p.value {
            Some(x) => u64::from(x),
            None => v,
        };
        total = total.wrapping_add(pushed) & mask;
        v = v.wrapping_add(u64::from(p.step)) & mask;
    }
    let mut live_sum: u64 = 0;
    for k in 0..p.live as u64 {
        live_sum = live_sum.wrapping_add(3 * k + 1 + (k + 2) * p.pushes);
    }
    (source, format!("{} {total} {live_sum}\n", p.pushes))
}

/// What the executable for arm64 prints of a program that pushes to a list of
/// each width, a literal or a number that changes, with few or many numbers
/// live, is what the evaluator prints, and what the program says it should.
fn what_an_arm64_executable_pushes_is_what_the_evaluator_says_at_every_width(part: u64) {
    generate::check_part(5112, 14, part, 3, pushes, |p| {
        let (source, printed) = pushed(p);
        let program = lower(source.as_bytes()).unwrap_or_else(|e| panic!("{e:?} for {source}"));
        let said = scalar::run(&program);
        assert_eq!(said.stdout, printed.clone().into_bytes(), "for {source}");
        let mut ran = 0;
        for target in Target::ALL {
            let bytes = compile(source.as_bytes(), *target).unwrap();
            if aarch64::is_executable(&bytes) {
                ran += 1;
                assert_eq!(aarch64::run(&bytes).stdout, said.stdout, "for {source}");
            }
        }
        assert!(ran > 0, "no target is built for arm64");
    });
}
