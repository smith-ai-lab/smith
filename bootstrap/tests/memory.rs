//! How a program asks the system for memory: a few large pieces, from which
//! its lists and texts take room. The requests a program makes of the system
//! are compared with the bytes its lists and texts take, which the evaluator
//! counts as `run_taking` says.

use smith::scalar::handler::{self, PIECE};
use smith::scalar::{self, Op};

use crate::Test;
use crate::generate::{self, sample};
use crate::programs;
use crate::reprs;
use crate::text;
use crate::tuples;

pub const ONCE: &[Test] = &[
    (
        "a_program_asks_the_system_at_most_once_for_each_64_kb_it_uses_and_once_more",
        a_program_asks_the_system_at_most_once_for_each_64_kb_it_uses_and_once_more,
    ),
    (
        "a_program_that_pushes_for_a_long_time_asks_at_most_once_for_each_64_kb_it_uses_and_once_more",
        a_program_that_pushes_for_a_long_time_asks_at_most_once_for_each_64_kb_it_uses_and_once_more,
    ),
    (
        "the_lexer_on_a_text_of_measure_asks_at_most_once_for_each_64_kb_it_uses_and_once_more",
        the_lexer_on_a_text_of_measure_asks_at_most_once_for_each_64_kb_it_uses_and_once_more,
    ),
    (
        "a_program_prints_what_it_printed_when_it_asks_for_room",
        a_program_prints_what_it_printed_when_it_asks_for_room,
    ),
    (
        "a_request_the_system_refuses_ends_the_program_with_status_1_after_what_it_printed",
        a_request_the_system_refuses_ends_the_program_with_status_1_after_what_it_printed,
    ),
    (
        "a_program_that_asks_for_nothing_has_no_handler",
        a_program_that_asks_for_nothing_has_no_handler,
    ),
    (
        "taking_room_from_a_piece_that_has_it_calls_the_system_for_nothing",
        taking_room_from_a_piece_that_has_it_calls_the_system_for_nothing,
    ),
];

/// Runs the program, and gives what it printed, how many times the system was
/// asked for memory, and the bytes the program took, each as a multiple of 8.
fn runs(source: &[u8]) -> (scalar::Outcome, usize, u64) {
    let program = smith::lower(source)
        .unwrap_or_else(|e| panic!("{e:?} for {}", String::from_utf8_lossy(source)));
    let (outcome, requests) = scalar::run_counting(&program, usize::MAX);
    let (_, taken) = scalar::run_taking(&program, usize::MAX);
    // A request with the top bit set asks for what is left when it is that much, and so
    // is counted as if it took it, which only makes the limit looser.
    let used = (taken.iter())
        .map(|n| (*n as u64 & !(1 << 63)).next_multiple_of(8))
        .sum();
    (outcome, requests.len(), used)
}

/// Checks that the program prints what it should, and asks the system at most
/// once for each 64 KB it takes, and once more.
fn asks_in_large_pieces(source: &[u8], stdout: Option<&[u8]>) {
    let (outcome, requests, used) = runs(source);
    let shown = String::from_utf8_lossy(source);
    assert_eq!(outcome.status, 0, "for {shown}");
    if let Some(stdout) = stdout {
        assert_eq!(outcome.stdout, stdout, "for {shown}");
    }
    assert!(
        requests as u64 <= used.div_ceil(PIECE) + 1,
        "{requests} requests for {used} bytes, in {shown}"
    );
}

/// Every program a generator builds that makes lists and texts at run time.
fn a_program_asks_the_system_at_most_once_for_each_64_kb_it_uses_and_once_more() {
    for case in 0..8 {
        let c = sample(3, case, programs::program);
        asks_in_large_pieces(&c.source, Some(&c.stdout));
        let c = sample(37, case, text::program);
        asks_in_large_pieces(c.source.as_bytes(), Some(&c.stdout));
        let c = sample(64, case, reprs::program);
        asks_in_large_pieces(c.source.as_bytes(), Some(&c.stdout));
        let c = sample(28, case, tuples::program);
        asks_in_large_pieces(c.source.as_bytes(), Some(&c.stdout));
    }
}

/// A list that grows by pushing, to sizes that take many pieces.
fn a_program_that_pushes_for_a_long_time_asks_at_most_once_for_each_64_kb_it_uses_and_once_more() {
    for n in [1, 100, 5000, 40000] {
        let source = format!(
            "fn main() ! alloc {{\n  var xs: List[u64] = []\n  var ys: List[u8] = []\n  loop i in 0..{n} {{\n    xs = push(xs, i)\n    ys = push(ys, 1)\n  }}\n}}\n"
        );
        asks_in_large_pieces(source.as_bytes(), None);
    }
}

/// The text of `measure/`: one piece of Smith source, repeated.
const PIECE_OF_SOURCE: &str =
    "// add two numbers\nfn add(a: u64, b: u64) -> u64 {\n  s = \"two\"\n  a + b * 0x1F - 3\n}\n";

/// The lexer in `compiler/lexer.smith` on the text `measure/` gives it, cut to
/// 504 bytes, since the evaluator takes 0.7 s for the 4,032 that the issue
/// counts, which is too long for a test. On those 4,032 it asked the system
/// 1,720 times, and now asks 3 times.
fn the_lexer_on_a_text_of_measure_asks_at_most_once_for_each_64_kb_it_uses_and_once_more() {
    let path = concat!(env!("CARGO_MANIFEST_DIR"), "/../compiler/lexer.smith");
    let lexer = std::fs::read_to_string(path).expect("compiler/lexer.smith");
    let lexer = &lexer[..lexer.find("\nfn main()").expect("a main") + 1];
    let pieces = 504 / PIECE_OF_SOURCE.len();
    let bytes: Vec<String> = PIECE_OF_SOURCE.bytes().map(|b| b.to_string()).collect();
    let source = format!(
        "{lexer}\nfn piece() -> List[u8] ! alloc {{\n  [{}]\n}}\n\nfn main() ! console, alloc {{\n  p = piece()\n  var src: List[u8] = []\n  loop r in 0..{pieces} {{\n    loop j in 0..p.size {{ src = push(src, p[j]) }}\n  }}\n  match lex(src) {{\n    .ok(tokens) {{ print(\"{{tokens.size}}\") }}\n    .err(errors) {{ print(\"error\") }}\n  }}\n}}\n",
        bytes.join(", ")
    );
    asks_in_large_pieces(source.as_bytes(), None);
    let program = smith::lower(source.as_bytes()).expect("compiles");
    assert_ne!(scalar::run(&program).stdout, b"error\n");
}

/// What a program prints before it builds a list and after it is what it
/// printed when the system was asked for the list alone.
fn a_program_prints_what_it_printed_when_it_asks_for_room() {
    let source = b"fn main() ! console, alloc {\n  print(\"before\")\n  xs: List[u8] = [1, 2, 3]\n  print(\"{xs.size}\")\n}\n";
    asks_in_large_pieces(source, Some(b"before\n3\n"));
}

/// A system that gives nothing, or too little for a piece, ends the program
/// with status 1 at the first place it needs room, and not before.
fn a_request_the_system_refuses_ends_the_program_with_status_1_after_what_it_printed() {
    let source = b"fn main() ! console, alloc {\n  print(\"before\")\n  xs: List[u8] = [1, 2, 3]\n  print(\"{xs.size}\")\n}\n";
    let program = smith::lower(source).expect("compiles");
    for limit in [0, 1, 100, PIECE as usize - 1] {
        let outcome = scalar::run_with_limit(&program, limit);
        assert_eq!(outcome.status, 1, "with a limit of {limit}");
        assert_eq!(outcome.stdout, b"before\n", "with a limit of {limit}");
    }
    let outcome = scalar::run_with_limit(&program, PIECE as usize);
    assert_eq!((outcome.status, outcome.stdout), (0, b"before\n3\n".to_vec()));
}

/// A program that builds no list and no text at run time has no handler, and
/// one that has the handler asks for room somewhere.
fn a_program_that_asks_for_nothing_has_no_handler() {
    let quiet = smith::lower(b"fn main() ! console {\n  print(\"hi\")\n}\n").expect("compiles");
    assert!(!quiet.allocates());
    assert!(!quiet.functions.contains(&handler::take()));
    generate::check(311, 20, programs::program, |case| {
        let program = smith::lower(&case.source).expect("compiles");
        let asks = program.functions.iter().any(|f| handler::takes_room(&program, f));
        assert_eq!(program.allocates(), asks, "for {}", String::from_utf8_lossy(&case.source));
    });
}

/// The blocks of `take` that give room from the piece have no call.
fn taking_room_from_a_piece_that_has_it_calls_the_system_for_nothing() {
    let take = handler::take();
    let asking: Vec<usize> = take
        .blocks
        .iter()
        .enumerate()
        .filter(|(_, b)| b.instructions.iter().any(|i| i.op.is_call()))
        .map(|(k, _)| k)
        .collect();
    assert_eq!(asking.len(), 1, "one block asks the system");
    // The first block ends in a choice of two, and the first is the one that
    // gives room.
    let scalar::End::Switch { jumps, .. } = &take.blocks[0].end else {
        panic!("the first block does not choose")
    };
    assert_eq!(jumps.len(), 2);
    assert_ne!(jumps[0].block, asking[0]);
    let fast = &take.blocks[jumps[0].block];
    assert!(fast.instructions.iter().all(|i| !matches!(i.op, Op::System { .. } | Op::Call { .. })));
    // An add and a subtraction, which are all it does.
    assert!(fast.instructions.len() <= 2, "{:?}", fast.instructions);
}
